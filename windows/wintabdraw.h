#ifndef WINTABDRAW_H
#define WINTABDRAW_H

typedef enum{	
    SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM
}SIDE;

void DrawHalfRoundFrame(HDC hdc, RECT const *pRect, SIDE side, 
    int radius, COLORREF clrBorder, COLORREF clrBack);
void DrawFrame(HDC hdc, POINT const *pPoints, int iCount, 
    COLORREF clrBorder, COLORREF clrBack);
void _DrawFrame(HDC hdc, POINT const *pPoints, int iCount, COLORREF clrLine);

#endif /* WINTABDRAW_H */

