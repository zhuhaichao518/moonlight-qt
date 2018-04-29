#include "nvhttp.h"

#include <QUuid>
#include <QtNetwork/QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QXmlStreamReader>

#define REQUEST_TIMEOUT_MS 5000

NvHTTP::NvHTTP(QString address) :
    m_Address(address)
{
    m_BaseUrlHttp.setScheme("http");
    m_BaseUrlHttps.setScheme("https");
    m_BaseUrlHttp.setHost(address);
    m_BaseUrlHttps.setHost(address);
    m_BaseUrlHttp.setPort(47989);
    m_BaseUrlHttps.setPort(47984);
}

NvComputer
NvHTTP::getComputerInfo()
{
    NvComputer computer;
    QString serverInfo = getServerInfo();

    computer.m_Name = getXmlString(serverInfo, "hostname");
    if (computer.m_Name == nullptr)
    {
        computer.m_Name = "UNKNOWN";
    }

    computer.m_Uuid = getXmlString(serverInfo, "uniqueid");
    computer.m_MacAddress = getXmlString(serverInfo, "mac");

    // If there's no LocalIP field, use the address we hit the server on
    computer.m_LocalAddress = getXmlString(serverInfo, "LocalIP");
    if (computer.m_LocalAddress == nullptr)
    {
        computer.m_LocalAddress = m_Address;
    }

    // If there's no ExternalIP field, use the address we hit the server on
    computer.m_RemoteAddress = getXmlString(serverInfo, "ExternalIP");
    if (computer.m_RemoteAddress == nullptr)
    {
        computer.m_RemoteAddress = m_Address;
    }

    computer.m_PairState = getXmlString(serverInfo, "PairStatus") == "1" ?
                NvComputer::PS_PAIRED : NvComputer::PS_NOT_PAIRED;

    computer.m_RunningGameId = getCurrentGame(serverInfo);

    computer.m_State = NvComputer::CS_ONLINE;

    return computer;
}

QVector<int>
NvHTTP::getServerVersionQuad(QString serverInfo)
{
    QString quad = getXmlString(serverInfo, "appversion");
    QStringList parts = quad.split(".");
    QVector<int> ret;

    for (int i = 0; i < 4; i++)
    {
        ret.append(parts.at(i).toInt());
    }
}

int
NvHTTP::getCurrentGame(QString serverInfo)
{
    // GFE 2.8 started keeping currentgame set to the last game played. As a result, it no longer
    // has the semantics that its name would indicate. To contain the effects of this change as much
    // as possible, we'll force the current game to zero if the server isn't in a streaming session.
    QString serverState = getXmlString(serverInfo, "state");
    if (serverState != nullptr && serverState.endsWith("_SERVER_BUSY"))
    {
        return getXmlString(serverInfo, "currentgame").toInt();
    }
    else
    {
        return 0;
    }
}

QString
NvHTTP::getServerInfo()
{
    QString serverInfo;

    try
    {
        // Always try HTTPS first, since it properly reports
        // pairing status (and a few other attributes).
        serverInfo = openConnectionToString(m_BaseUrlHttps,
                                            "serverinfo",
                                            nullptr,
                                            true);
        // Throws if the request failed
        verifyResponseStatus(serverInfo);
    }
    catch (GfeHttpResponseException& e)
    {
        if (e.getStatusCode() == 401)
        {
            // Certificate validation error, fallback to HTTP
            serverInfo = openConnectionToString(m_BaseUrlHttps,
                                                "serverinfo",
                                                nullptr,
                                                true);
            verifyResponseStatus(serverInfo);
        }
    }

    return serverInfo;
}

void
NvHTTP::verifyResponseStatus(QString xml)
{
    QXmlStreamReader xmlReader(xml);

    while (xmlReader.readNextStartElement())
    {
        if (xmlReader.name() == "root")
        {
            int statusCode = xmlReader.attributes().value("status_code").toInt();
            if (statusCode == 200)
            {
                // Successful
                return;
            }
            else
            {
                QString statusMessage = xmlReader.attributes().value("status_message").toString();
                qDebug() << "Request failed: " << statusCode << " " << statusMessage;
                throw GfeHttpResponseException(statusCode, statusMessage);
            }
        }
    }
}

QString
NvHTTP::getXmlString(QString xml,
                     QString tagName)
{
    QXmlStreamReader xmlReader(xml);

    while (xmlReader.readNextStartElement())
    {
        if (xmlReader.name() == tagName)
        {
            return xmlReader.text().toString();
        }
    }

    return nullptr;
}

QString
NvHTTP::openConnectionToString(QUrl baseUrl,
                               QString command,
                               QString arguments,
                               bool enableTimeout)
{
    QNetworkReply* reply = openConnection(baseUrl, command, arguments, enableTimeout);
    QString ret;

    ret = QTextStream(reply).readAll();
    delete reply;

    return ret;
}

QNetworkReply*
NvHTTP::openConnection(QUrl baseUrl,
                       QString command,
                       QString arguments,
                       bool enableTimeout)
{
    // Build a URL for the request
    QUrl url(baseUrl);
    url.setPath(command +
                "?uniqueid=" + "0" +
                "&uuid=" + QUuid::createUuid().toString() +
                ((arguments != nullptr) ? (arguments + "&") : ""));

    QNetworkReply* reply = m_Nam.get(QNetworkRequest(url));

    // Ignore self-signed certificate errors (since GFE uses them)
    reply->ignoreSslErrors(QList<QSslError>{ QSslError::SelfSignedCertificate });

    // Run the request with a timeout if requested
    QEventLoop loop;
    QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    if (enableTimeout)
    {
        QTimer::singleShot(REQUEST_TIMEOUT_MS, &loop, SLOT(quit()));
    }
    qDebug() << "Executing request: " << url.toString();
    loop.exec(QEventLoop::ExcludeUserInputEvents);

    // Abort the request if it timed out
    if (!reply->isFinished())
    {
        qDebug() << "Aborting timed out request for " << url.toString();
        reply->abort();
    }

    // Handle error
    if (reply->error() != QNetworkReply::NoError)
    {
        qDebug() << command << " request for failed with error " << reply->error();
        GfeHttpResponseException* exception = new GfeHttpResponseException(reply->error(), reply->errorString());
        delete reply;
        throw exception;
    }

    return reply;
}
