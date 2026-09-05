
// SvgView.h - header for SvgView project
#ifndef SVGVIEW_H
#define SVGVIEW_H

#ifdef __cplusplus
extern "C" {
#endif

// resvg functions
int LoadSvgResvg(const char* filepath);
unsigned char* GetResvgPixels(void);  // <-- FIXED: was void*
int GetResvgWidth(void);
int GetResvgHeight(void);
void FreeResvg(void);

// nanosvg fallback functions
int LoadSvgNanosvg(const char* filepath);
void FreeSvgNanosvg(void);

// unified loader
int LoadSvg(const char* filepath);
void FreeSvg(void);

#ifdef __cplusplus
}
#endif

#endif // SVGVIEW_H
