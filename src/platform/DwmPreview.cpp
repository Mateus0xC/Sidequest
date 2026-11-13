#include "DwmPreview.h"
#include <algorithm>

namespace sq {

bool DwmPreview::attach(HWND dest, HWND src, const std::string& corner, int widthPx, int margin, int clientW,
                        int clientH) {
    detach();
    if (!src || !IsWindow(src)) return false;
    if (FAILED(DwmRegisterThumbnail(dest, src, &thumb_))) { thumb_ = nullptr; return false; }
    SIZE ss{};
    if (FAILED(DwmQueryThumbnailSourceSize(thumb_, &ss)) || ss.cx <= 0 || ss.cy <= 0) { detach(); return false; }
    int w = widthPx;
    int h = (int)((long long)w * ss.cy / ss.cx);
    int x = corner.find("left") != std::string::npos ? margin : clientW - margin - w;
    int y = corner.find("top") != std::string::npos ? margin : clientH - margin - h;
    DWM_THUMBNAIL_PROPERTIES p{};
    p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    p.rcDestination = {x, y, x + w, y + h};
    p.fVisible = TRUE;
    p.opacity = 255;
    p.fSourceClientAreaOnly = TRUE;
    if (FAILED(DwmUpdateThumbnailProperties(thumb_, &p))) { detach(); return false; }
    return true;
}

void DwmPreview::detach() {
    if (thumb_) { DwmUnregisterThumbnail(thumb_); thumb_ = nullptr; }
}
}  // namespace sq
