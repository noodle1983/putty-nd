/*
 * wintabdraw.c - the implementation of the tabbar self draw.
 */

#ifndef NO_MULTIMON
#include <multimon.h>
#endif

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>

#include "putty.h"
#include "terminal.h"
#include "storage.h"
#include "win_res.h"
#include "wintabdraw.h"

HRGN DrawChromeFrame(HDC hdc, RECT *pRect, COLORREF clrBorder, COLORREF clrBack)
{
    HBRUSH hBackBrush = NULL;
    HBRUSH hBorderBrush;
    HRGN hRgn;

    hBorderBrush = CreateSolidBrush(clrBorder);
    hBackBrush = CreateSolidBrush (clrBack);

    POINT lpts[4], rpts[4];
    int spread, eigth, sixth, quarter;
    int width = pRect->right - pRect->left;
    int height = pRect->bottom - pRect->top;

    if (1){//bottom
        spread = ((float)height) * 2/3;
        eigth = ((float)height) * 1/8;
        sixth = ((float)height) * 1/6;
        quarter = ((float)height) * 1/4;    
    }else{
        spread = ((float)width) * 2/3;
        eigth = ((float)width) * 1/8;
        sixth = ((float)width) * 1/6;
        quarter = ((float)width) * 1/4;
    }
    
    pRect->right += spread;
    
    lpts[3].x = pRect->left;                     lpts[3].y = pRect->bottom;
	lpts[2].x = pRect->left + sixth;             lpts[2].y = pRect->bottom - eigth;
	lpts[1].x = pRect->left + spread - quarter;  lpts[1].y = pRect->top + eigth;
	lpts[0].x = pRect->left + spread;            lpts[0].y = pRect->top;

    rpts[3].x = pRect->right - spread;           rpts[3].y = pRect->top;
	rpts[2].x = pRect->right - spread + quarter; rpts[2].y = pRect->top + eigth;
	rpts[1].x = pRect->right - sixth;            rpts[1].y = pRect->bottom - eigth;
	rpts[0].x = pRect->right;                    rpts[0].y = pRect->bottom;
    MoveToEx(hdc, lpts[3].x, lpts[3].y, NULL);
    BeginPath(hdc);
    
    PolyBezier(hdc, lpts, sizeof(lpts)/sizeof(POINT));
    
    //MoveToEx(hdc, lpts[0].x, lpts[0].y, NULL);
	LineTo(hdc, rpts[3].x, rpts[3].y);
    
    PolyBezier(hdc, rpts, sizeof(rpts)/sizeof(POINT));
    
    //MoveToEx(hdc, rpts[0].x, rpts[0].y, NULL);
    LineTo(hdc, lpts[3].x, lpts[3].y);

    CloseFigure(hdc);
    EndPath(hdc);
    //StrokePath (hdc);
    FlattenPath(hdc);
    hRgn = PathToRegion(hdc);
    FillRgn(hdc, hRgn, hBackBrush);
    FrameRgn(hdc, hRgn, hBorderBrush, 1, 1);

    DeleteObject(hBorderBrush);    
    DeleteObject(hBackBrush);

    pRect->left += spread;
    pRect->right -= spread;
    return hRgn;
}

int DrawSysButtonFrame(HDC hdc, RECT *pRect, COLORREF clrBorder, COLORREF clrBack, HRGN* hRgns)
{
    HBRUSH hBackBrush;
    HBRUSH hBorderBrush;
    HRGN hRgn;
    int width = pRect->right - pRect->left;
    POINT prt[2][4] = {
{{pRect->left, pRect->top},    {pRect->left + width*0.31, pRect->top},    {pRect->left + width*0.62, pRect->top},    {pRect->right, pRect->top}},
{{pRect->left, pRect->bottom}, {pRect->left + width*0.31, pRect->bottom}, {pRect->left + width*0.62, pRect->bottom}, {pRect->right, pRect->bottom}}
        };
    int radius = 3;

    hBorderBrush = CreateSolidBrush(clrBorder);
    //hBorderBrush = GetSysColorBrush(COLOR_3DSHADOW);
    hBackBrush = CreateSolidBrush (clrBack);

    MoveToEx(hdc, prt[0][0].x, prt[0][0].y, NULL);
    BeginPath(hdc);
    LineTo(hdc, prt[1][0].x, prt[1][0].y - radius);
    AngleArc(hdc, prt[1][0].x + radius, prt[1][0].y - radius,
            radius,180, 90);
    LineTo(hdc, prt[1][1].x, prt[1][1].y);
    LineTo(hdc, prt[0][1].x, prt[0][1].y);
    CloseFigure(hdc);
    EndPath(hdc);
    hRgn = PathToRegion(hdc);
    FillRgn(hdc, hRgn, hBackBrush);
    FrameRgn(hdc, hRgn, hBorderBrush, 1, 1);
    hRgns[0] = hRgn;

    MoveToEx(hdc, prt[0][1].x, prt[0][1].y, NULL);
    BeginPath(hdc);
    LineTo(hdc, prt[1][1].x, prt[1][1].y);
    LineTo(hdc, prt[1][2].x, prt[1][2].y);
    LineTo(hdc, prt[0][2].x, prt[0][2].y);
    CloseFigure(hdc);
    EndPath(hdc);
    hRgn = PathToRegion(hdc);
    FillRgn(hdc, hRgn, hBackBrush);
    FrameRgn(hdc, hRgn, hBorderBrush, 1, 1);
    hRgns[1] = hRgn;

    MoveToEx(hdc, prt[0][2].x, prt[0][2].y, NULL);
    BeginPath(hdc);
    LineTo(hdc, prt[1][2].x, prt[1][2].y);
    LineTo(hdc, prt[1][3].x - radius, prt[1][3].y);
    AngleArc(hdc, prt[1][3].x - radius, prt[1][3].y - radius,
            radius,270, 90);
    LineTo(hdc, prt[0][3].x, prt[0][3].y);
    CloseFigure(hdc);
    EndPath(hdc);
    hRgn = PathToRegion(hdc);
    FillRgn(hdc, hRgn, hBackBrush);
    FrameRgn(hdc, hRgn, hBorderBrush, 1, 1);
    hRgns[2] = hRgn;
    
    //DeleteObject(hBorderBrush);    
    DeleteObject(hBackBrush);
    return 0;
}

int DrawSysButton(HDC hdc, RECT *pRect, COLORREF clrBorder)
{
    HGDIOBJ hPen = NULL;
    HGDIOBJ hOldPen; 
    int i;
    POINT prt[3];
    
    int width = pRect->right - pRect->left;
    RECT rc[3] = {
        {pRect->left, pRect->top, pRect->left + width*0.31, pRect->bottom},
        {pRect->left + width*0.31, pRect->top, pRect->left + width*0.62, pRect->bottom},
        {pRect->left + width*0.62, pRect->top, pRect->right, pRect->bottom}
    };
    for (i = 0; i < 3; i++){
        prt[i].x = (rc[i].left + rc[i].right)/2;
        prt[i].y = (rc[i].top + rc[i].bottom)/2;
    }

    hPen = CreatePen(PS_SOLID, 2, clrBorder);
    hOldPen = SelectObject(hdc, hPen);

    DrawLine(hdc, prt[0].x - 4, prt[0].y, 
                  prt[0].x + 4, prt[0].y);

    DrawRect4(hdc, prt[1].x - 5, prt[1].y - 3, 
                   prt[1].x + 5, prt[1].y + 3);

    DrawLine(hdc, prt[2].x - 4, prt[2].y - 4, 
                  prt[2].x + 4, prt[2].y + 4);
    DrawLine(hdc, prt[2].x - 4, prt[2].y + 4,
                  prt[2].x + 4, prt[2].y - 4);

    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    return 0;
}

HRGN DrawCloseButton(HDC hdc, const int x, const int y, 
    COLORREF clrBorder, COLORREF clrBack )
{
    HGDIOBJ hPen = NULL;
    HGDIOBJ hOldPen; 
    HBRUSH hBrush = NULL;
    HBRUSH hOldBrush;

    hPen = CreatePen(PS_SOLID, 2, clrBorder);
    hOldPen = SelectObject(hdc, hPen); 

    hBrush = CreateSolidBrush (clrBack);
    hOldBrush = SelectObject(hdc, hBrush);

    BeginPath(hdc);
    AngleArc(hdc, x , y, 6, 0, 360);
    EndPath(hdc);
    HRGN hRgn = PathToRegion(hdc);
    FillRgn(hdc, hRgn, hBrush);
    
    DrawLine(hdc, x - 4, y - 4, 
                  x + 4, y + 4);
    DrawLine(hdc, x - 4, y + 4,
                  x + 4, y - 4);
    
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
    DeleteObject(hBrush);
    return hRgn;
}

void DrawHalfRoundFrame(HDC hdc, RECT const *pRect, SIDE side, int radius, COLORREF clrBorder, COLORREF clrBack)
{	
    POINT pts[6];
	switch(side)
	{	case SIDE_LEFT:
			pts[0].x = pRect->right-1; pts[0].y = pRect->top;
			pts[1].x = pRect->left+radius; pts[1].y = pRect->top;
			pts[2].x = pRect->left; pts[2].y = pRect->top+radius;
			pts[3].x = pRect->left; pts[3].y = pRect->bottom-1-radius;
			pts[4].x = pRect->left+radius; pts[4].y = pRect->bottom-1;
			pts[5].x = pRect->right-1; pts[5].y = pRect->bottom-1;
			break;
		case SIDE_TOP:
			pts[0].x = pRect->left; pts[0].y = pRect->bottom-1;
			pts[1].x = pRect->left; pts[1].y = pRect->top+radius;
			pts[2].x = pRect->left+radius; pts[2].y = pRect->top;
			pts[3].x = pRect->right-1-radius; pts[3].y = pRect->top;
			pts[4].x = pRect->right-1; pts[4].y = pRect->top+radius;
			pts[5].x = pRect->right-1; pts[5].y = pRect->bottom-1;
			break;
		case SIDE_RIGHT:
			pts[0].x = pRect->left; pts[0].y = pRect->top;
			pts[1].x = pRect->right-1-radius; pts[1].y = pRect->top;
			pts[2].x = pRect->right-1; pts[2].y = pRect->top+radius;
			pts[3].x = pRect->right-1; pts[3].y = pRect->bottom-1-radius;
			pts[4].x = pRect->right-1-radius; pts[4].y = pRect->bottom-1;
			pts[5].x = pRect->left; pts[5].y = pRect->bottom-1;
			break;
		case SIDE_BOTTOM:
			pts[0].x = pRect->left; pts[0].y = pRect->top;
			pts[1].x = pRect->left; pts[1].y = pRect->bottom-1-radius;
			pts[2].x = pRect->left+radius; pts[2].y = pRect->bottom-1;
			pts[3].x = pRect->right-1-radius; pts[3].y = pRect->bottom-1;
			pts[4].x = pRect->right-1; pts[4].y = pRect->bottom-1-radius;
			pts[5].x = pRect->right-1; pts[5].y = pRect->top;
			break;
	}
		 
	DrawFrame(hdc,pts,sizeof(pts)/sizeof(POINT),clrBorder,clrBack);
}

void DrawFrame(HDC hdc, POINT const *pPoints, int iCount, COLORREF clrBorder, COLORREF clrBack)
{	
    HGDIOBJ hPen = NULL;
    HGDIOBJ hOldPen; 
    HBRUSH hBrush = NULL;
    HBRUSH hOldBrush;

    hPen = CreatePen(PS_SOLID, 1, clrBorder);
    hOldPen = SelectObject(hdc, hPen); 

    hBrush = CreateSolidBrush (clrBack);
    hOldBrush = SelectObject(hdc, hBrush);

	Polygon(hdc, (POINT *)pPoints,iCount);

	SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
	SelectObject(hdc, hOldBrush); 
    DeleteObject(hBrush);

	_DrawFrame(hdc,pPoints,iCount,clrBorder);
}

void _DrawFrame(HDC hdc, POINT const *pPoints, int iCount, COLORREF clrLine)
{	
    HGDIOBJ hPen = NULL;
    HGDIOBJ hOldPen; 
    int i;

    hPen = CreatePen(PS_SOLID, 1, clrLine);
    hOldPen = SelectObject(hdc, hPen); 

	MoveToEx(hdc, pPoints[0].x, pPoints[0].y, NULL);
	for(i = 1; i < iCount; ++i)
		LineTo(hdc, pPoints[i].x, pPoints[i].y);
	SetPixelV(hdc, pPoints[iCount-1].x, pPoints[iCount-1].y, clrLine);

	SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}

void DrawGradient(HDC hdc, RECT const *pRect, char horz, COLORREF clrTopLeft, COLORREF clrBottomRight)
{	
    GRADIENT_RECT gRect = {0,1};
	TRIVERTEX vert[2] = {	
        {pRect->left,pRect->top,(COLOR16)(GetRValue(clrTopLeft)<<8),(COLOR16)(GetGValue(clrTopLeft)<<8),(COLOR16)(GetBValue(clrTopLeft)<<8),0},
		{pRect->right,pRect->bottom,(COLOR16)(GetRValue(clrBottomRight)<<8),(COLOR16)(GetGValue(clrBottomRight)<<8),(COLOR16)(GetBValue(clrBottomRight)<<8),0}
	};
	GradientFill(hdc,vert,2,&gRect,1,(horz==TRUE ? GRADIENT_FILL_RECT_H : GRADIENT_FILL_RECT_V));
}

void DrawLinec(HDC hdc, int x1, int y1, int x2, int y2, COLORREF clrLine)
{
    HGDIOBJ hPen = NULL;
    HGDIOBJ hOldPen; 

    hPen = CreatePen(PS_SOLID, 1, clrLine);
    hOldPen = SelectObject(hdc, hPen); 
    
	MoveToEx(hdc, x1, y1, NULL);
	LineTo(hdc, x2, y2);
    
	SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}

void DrawLine(HDC hdc, int x1, int y1, int x2, int y2)
{
	MoveToEx(hdc, x1, y1, NULL);
	LineTo(hdc, x2, y2);
}

void DrawRect4c(HDC hdc, int x1, int y1, int x2, int y2, COLORREF clrLine)
{
    HGDIOBJ hPen = NULL;
    HGDIOBJ hOldPen; 

    hPen = CreatePen(PS_SOLID, 1, clrLine);
    hOldPen = SelectObject(hdc, hPen); 
	MoveToEx(hdc, x1, y1, NULL);
	LineTo(hdc, x1, y2);
	LineTo(hdc, x2, y2);
	LineTo(hdc, x2, y1);
	LineTo(hdc, x1, y1);
	SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}

void DrawRect1c(HDC hdc, RECT const *pRect, COLORREF clrLine)
{	
    DrawRect4c(hdc, pRect->left, pRect->top, pRect->right-1, pRect->bottom-1, clrLine);
}
	
void DrawRect4(HDC hdc, int x1, int y1, int x2, int y2)
{	
    MoveToEx(hdc, x1, y1, NULL);
	LineTo(hdc, x1, y2);
	LineTo(hdc, x2, y2);
	LineTo(hdc, x2, y1);
	LineTo(hdc, x1, y1);
}
	
void DrawRect1(HDC hdc, RECT const *pRect)
{	
    MoveToEx(hdc, pRect->left, pRect->top, NULL);
	LineTo(hdc, pRect->left,pRect->bottom-1);
	LineTo(hdc, pRect->right-1,pRect->bottom-1);
	LineTo(hdc, pRect->right-1,pRect->top);
	LineTo(hdc, pRect->left,pRect->top);
    
}

void DrawBeveledRect(HDC hdc, RECT const *pRect, int bevel)
{	
    MoveToEx(hdc, pRect->left,pRect->top+bevel, NULL);
	LineTo(hdc, pRect->left,pRect->bottom-bevel-1);
	LineTo(hdc, pRect->left+bevel,pRect->bottom-1);
	LineTo(hdc, pRect->right-bevel-1,pRect->bottom-1);
	LineTo(hdc, pRect->right-1,pRect->bottom-bevel-1);
	LineTo(hdc, pRect->right-1,pRect->top+bevel);
	LineTo(hdc, pRect->right-bevel-1,pRect->top);
	LineTo(hdc, pRect->left+bevel,pRect->top);
	LineTo(hdc, pRect->left,pRect->top+bevel);
}

COLORREF PixelAlpha(COLORREF src, COLORREF dst, int percent)
{	
    int ipercent = 100 - percent;
	return RGB(
		(GetRValue(src) * percent + GetRValue(dst) * ipercent) / 100,
		(GetGValue(src) * percent + GetGValue(dst) * ipercent) / 100,
		(GetBValue(src) * percent + GetBValue(dst) * ipercent) / 100);
}

//void FillSolidRect(HDC hdc, RECT const *rc, COLORREF color)
//{	
//    _FillSolidRect(hdc,rc,color);
//}
	
void FillSolidRect(HDC hdc, RECT const *rc, COLORREF color)
{ 
    HBRUSH hBrush = NULL;
    HBRUSH hOldBrush;

    hBrush = CreateSolidBrush (color);
    hOldBrush = SelectObject(hdc, hBrush);
	FillRect(hdc, rc, hBrush);
	SelectObject(hdc, hOldBrush); 
    DeleteObject(hBrush);
}

	 
int DrawMarker(HDC hdc, RECT const *pRect, HBITMAP hbmpMask, COLORREF color)
{	
    BITMAP maskInfo;
	if(GetObject(hbmpMask,sizeof(maskInfo),&maskInfo)==0) return FALSE;
		 
    int width = min(pRect->right - pRect->left, maskInfo.bmWidth);
    int height = min(pRect->bottom - pRect->top, maskInfo.bmHeight);
		 
	HDC dcMask=NULL, dc1=NULL, dc2=NULL;
	HBITMAP bmp1=NULL, bmp2=NULL;
		 
	int res = FALSE;
		 
	if((dcMask = CreateCompatibleDC(hdc))!=NULL &&
		(dc1 = CreateCompatibleDC(hdc))!=NULL && 
		(dc2 = CreateCompatibleDC(hdc))!=NULL &&
		(bmp1 = CreateCompatibleBitmap(hdc,width,height))!=NULL && 
		(bmp2 = CreateCompatibleBitmap(hdc,width,height))!=NULL)
	{
		HBITMAP bmpMaskOld = (HBITMAP)SelectObject(dcMask,hbmpMask);
		HBITMAP bmpOld1 = (HBITMAP)SelectObject(dc1,bmp1);
		HBITMAP bmpOld2 = (HBITMAP)SelectObject(dc2,bmp2);
			 
		BitBlt(dc1,0,0,width,height,dcMask,0,0,SRCCOPY);
			 
		BitBlt(hdc,pRect->left,pRect->top,width,height,dc1,0,0,SRCAND);
			 
		BitBlt(dc1,0,0,width,height,NULL,0,0,DSTINVERT);
			 
		HBRUSH br = CreateSolidBrush(color);
		HBRUSH brOld = (HBRUSH)SelectObject(dc2,br);
		BitBlt(dc2,0,0,width,height,dc1,0,0,MERGECOPY);
		SelectObject(dc2,brOld);
		DeleteObject(br);
			 
		BitBlt(hdc,pRect->left,pRect->top,width,height,dc2,0,0,SRCPAINT);
			 
		SelectObject(dcMask,bmpMaskOld);
		SelectObject(dc1,bmpOld1);
		SelectObject(dc2,bmpOld2);
			 
		res = TRUE;
	}
		 
	if(bmp1!=NULL) DeleteObject(bmp1);
	if(bmp2!=NULL) DeleteObject(bmp2);
	if(dcMask!=NULL) DeleteDC(dcMask);
	if(dc1!=NULL) DeleteDC(dc1);
	if(dc2!=NULL) DeleteDC(dc2);
		 
	return res;
}

	 
int CorrectFitSpaceString(HDC hdc, char const *strSrc, int maxLength, char *strDst)
{	
    int strSrcLength = strlen(strSrc);
		 
	int count;
	SIZE sz;
    SIZE dot3sz;
	if(GetTextExtentExPoint(hdc, strSrc,strSrcLength,maxLength,&count,NULL,&sz)!=0){
		if(count < strSrcLength){	
            if(count>0){
                GetTextExtentPoint32(hdc, TEXT("..."), strlen(TEXT("...")), &dot3sz);
                int pointsWidth = dot3sz.cx;
				if(GetTextExtentExPoint(hdc,strSrc,strSrcLength,max(0,maxLength-pointsWidth),&count,NULL,&sz)==0) return FALSE;
                memcpy(strDst, strSrc, count);
                strncpy(strDst + count, TEXT("..."), maxLength - count - 1);
			}
			else
				strncpy(strDst, TEXT("..."), maxLength - 1);
			return TRUE;
		}
	}
		 
	return FALSE;
}


