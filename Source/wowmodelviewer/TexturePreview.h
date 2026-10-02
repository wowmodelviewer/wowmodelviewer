/*
 * TexturePreview.h
 *
 * The texture view's preview: one decoded texture (straight alpha), drawn without distortion, centred,
 * over a background, at a size chosen for it.
 *
 *  - Alpha view: On (RGBA over the background), Off (RGB, opaque), Only (the alpha channel as
 *    grey). Display only; the image itself is never changed.
 *  - Background under the image: checkerboard (cells anchored to the image), dark or light. Around
 *    it a fixed neutral surround, a margin and a hairline, so its edges always show.
 *  - Size (displayScale): there is no zoom. A texture larger than the area is scaled down smoothly
 *    to fit it. A smaller one is enlarged only by a whole number (drawn nearest-neighbour, every
 *    texel the same size), at most MaxEnlargement times, and only while its longer side stays within
 *    EnlargeUpTo (both in device-independent pixels): a small texture is never blown up into a large
 *    pixelated picture, and one of EnlargeUpTo or more is shown 1:1 when it fits. Decided again
 *    whenever the area changes size. Both numbers rest on a census of the client's textures: see
 *    their definitions.
 *  - The size is the texture's own (its level 0): the image given may be a smaller mip level of it,
 *    for a texture the area shows much smaller than it is (TextureView decides). It is only ever
 *    reduced to the size shown; when the area grows past it, onNeedsDetail asks for a larger level.
 *  - The picture over its background is composed once per image, size, alpha view and background,
 *    and kept as a bitmap: a repaint only draws it. While a dragged sash or window edge keeps changing
 *    a large image's size, it is composed roughly (nearest), then smoothly once the drag has paused.
 *  - A new image replaces the old one in one paint: nothing is dimmed or blanked in between. A small
 *    note in the corner (setNote) can say that a slow one is on its way.
 *
 * With no image it shows a message (nothing selected, an error and what still works).
 */

#ifndef TEXTUREPREVIEW_H
#define TEXTUREPREVIEW_H

#include <functional>

#include <QImage>
#include <QRect>
#include <QSize>

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/timer.h>

class TexturePreview : public wxWindow
{
public:
  enum class AlphaView { On, Off, Only };
  enum class Background { Checkerboard, Dark, Light };

  // How far a texture smaller than the area is enlarged: at most MaxEnlargement times, and not past
  // EnlargeUpTo on its longer side (device-independent pixels; see the .cpp).
  static const int MaxEnlargement;
  static const int EnlargeUpTo;

  explicit TexturePreview(wxWindow * parent);

  // The image to show: Format_ARGB32, straight alpha. 'textureSize' is the texture's own size (its level
  // 0) when the image is a smaller level of it; empty when the image is the texture itself. Replaces what
  // is shown at the next paint, sized and centred anew.
  void setImage(const QImage & image, const QSize & textureSize = QSize());
  // No image: a centred title and detail lines (the empty state, or why a texture is not shown).
  void setMessage(const wxString & title, const wxString & detail = wxEmptyString,
                  const wxString & hint = wxEmptyString);
  // A small note in the corner over whatever is shown ("Decoding ..."); empty removes it. setImage and
  // setMessage remove it too.
  void setNote(const wxString & note);
  bool hasImage() const { return !m_source.isNull(); }
  // Free the alpha view's copy, the scaled image and the composed picture; the next paint makes them again.
  void releaseCaches();
  const QImage & image() const { return m_source; }
  // The texture's own size (the image's, unless a smaller level was given).
  QSize textureSize() const;

  void setAlphaView(AlphaView view);
  AlphaView alphaView() const { return m_alphaView; }
  void setBackground(Background background);
  Background background() const { return m_background; }

  // Screen pixels per texel of the texture (its own size) for the current window size.
  double displayScale() const;
  // Where the texture is drawn, in window pixels (empty with no image).
  QRect displayRect() const;
  // The size a texture of this size would be drawn at in the current window (the same rule).
  QSize displaySizeFor(const QSize & textureSize) const;

  // Called once a new image has been painted the first time, with how long that paint took (ms).
  std::function<void(double)> onImagePainted;
  // The image is a smaller level and the area now shows the texture larger than it: the longer side
  // (screen pixels) a level must have to be reduced, not enlarged.
  std::function<void(int)> onNeedsDetail;

private:
  void OnPaint(wxPaintEvent & event);
  void OnSize(wxSizeEvent & event);
  void OnMouse(wxMouseEvent & event);
  void OnFocus(wxFocusEvent & event);

  double scaleFor(const QSize & textureSize, const wxSize & area) const;
  // The source converted for the alpha view (cached for the current view).
  const QImage & viewSource();
  // The picture for the current display rectangle, alpha view and background.
  void compose(const QRect & rect);
  void dropComposed();

  QImage m_source;
  QSize m_textureSize;         // the texture's own size when m_source is a smaller level; else empty
  QImage m_viewImage;          // m_source for On; an opaque or grey copy for Off/Only
  AlphaView m_viewImageFor = AlphaView::On;
  bool m_viewImageValid = false;
  QImage m_scaled;             // the view image scaled to the display size, when that is not 1:1 or n:1
  QSize m_scaledSize;
  bool m_scaledSmooth = false; // m_scaled is the smooth scale, not the quick one of a resize
  wxBitmap m_composed;         // the picture over its background, at the display size
  QSize m_composedSize;
  bool m_composedValid = false;
  wxTimer m_resizeSettle;      // runs from each resize until resizing has paused
  int m_detailAskedFor = 0;    // the side last asked for through onNeedsDetail

  AlphaView m_alphaView = AlphaView::On;
  Background m_background = Background::Checkerboard;
  bool m_reportPaint = false;  // a new image waits for its first paint
  int m_maxEnlargement = MaxEnlargement;
  int m_enlargeUpTo = EnlargeUpTo;

  wxString m_title, m_detail, m_hint;
  wxString m_note;
};

#endif // TEXTUREPREVIEW_H
