#ifndef EXPORT_H
#define EXPORT_H

#ifdef MOONLIGHT_LIBRARY_BUILD
#define MOONLIGHT_API __declspec(dllexport)
#else
#define MOONLIGHT_API __declspec(dllimport)
#endif

#endif // EXPORT_H
