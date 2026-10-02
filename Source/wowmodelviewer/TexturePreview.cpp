/*
 * TexturePreview.cpp -- see TexturePreview.h.
 */

#include "TexturePreview.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QElapsedTimer>
#include <QPainter>

#include <wx/dcbuffer.h>

// How far a texture smaller than the area is enlarged. From a census of the 786,335 textures client
// 12.1.0.69933 shows (their headers): the longer side is at most 64 for 7.1% of them, 128 for 14.2%,
// 256 for 61.8%, 512 for 95.2% and 1024 for 99.2%; 236 are 4096. The commonest sizes: 256 x 256 (40.0%),
// 512 x 512 (22.6%), 512 x 256 (9.0%), 64 x 64 (6.0%, the icons). Real textures of each size compared in
// the texture area of a maximised 2560 x 1440 window with the model panels away (2321 x 1118): at 3x and 4x
// a 64 icon becomes a 192 or 256 pixel picture of blocks and a 256 skin 768 or 1024 pixels, most of the
// area's height; at 2x the icon is 128 and the skin 512, each texel a crisp 2 x 2 square. A 512 texture at
// 2x still fills the area's height with blocks, so enlarging stops at 512: the size 95% of the client's
// textures are at most, and how a 512 texture looks at its own size.
const int TexturePreview::MaxEnlargement = 2;
const int TexturePreview::EnlargeUpTo = 512;

namespace
{
  // Around the image: darker than Light, lighter than Dark, so the image's extent always shows.
  const wxColour Surround(78, 78, 78);
  const wxColour SurroundText(236, 236, 236);
  const wxColour SurroundDetail(196, 196, 196);
  // The hairline around the image: black at 43% over the surround.
  const wxColour Hairline(44, 44, 44);

  // The space kept free around the image, in DIPs: the surround stays visible on every side.
  const int Margin = 16;
  // A 4096 texture in a window a few hundred pixels across still gets a scale.
  const double MinScale = 1.0 / 64;

  QRgb rgbOf(const wxColour & c) { return qRgb(c.Red(), c.Green(), c.Blue()); }

  wxColour backgroundColour(TexturePreview::Background background, bool second = false)
  {
    switch (background)
    {
      case TexturePreview::Background::Dark: return wxColour(28, 28, 28);
      case TexturePreview::Background::Light: return wxColour(236, 236, 236);
      default: return second ? wxColour(204, 204, 204) : wxColour(255, 255, 255);
    }
  }

  // The alpha view applied to (a copy of) the image: Off drops the alpha, Only shows it.
  QImage convertForView(const QImage & source, TexturePreview::AlphaView view)
  {
    if (view == TexturePreview::AlphaView::On)
      return source;
    QImage out = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < out.height(); y++)
    {
      QRgb * row = reinterpret_cast<QRgb *>(out.scanLine(y));
      for (int x = 0; x < out.width(); x++)
      {
        const QRgb p = row[x];
        if (view == TexturePreview::AlphaView::Off)
          row[x] = p | 0xff000000u;
        else
        {
          const int a = qAlpha(p);
          row[x] = qRgb(a, a, a);
        }
      }
    }
    return out;
  }

  // An opaque picture as a bitmap: Qt's own conversion to packed RGB, then row by row into the wxImage.
  wxBitmap toBitmap(const QImage & picture)
  {
    const QImage rgb = picture.convertToFormat(QImage::Format_RGB888);
    wxImage out(rgb.width(), rgb.height(), false);
    unsigned char * d = out.GetData();
    const int rowBytes = rgb.width() * 3;
    for (int y = 0; y < rgb.height(); y++)
      std::memcpy(d + (size_t)y * rowBytes, rgb.constScanLine(y), rowBytes);
    return wxBitmap(out);
  }
}

TexturePreview::TexturePreview(wxWindow * parent)
  : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE, wxT("texturePreview"))
{
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetMinSize(FromDIP(wxSize(240, 200)));
  Bind(wxEVT_PAINT, &TexturePreview::OnPaint, this);
  Bind(wxEVT_SIZE, &TexturePreview::OnSize, this);
  Bind(wxEVT_LEFT_DOWN, &TexturePreview::OnMouse, this);
  Bind(wxEVT_MOUSEWHEEL, &TexturePreview::OnMouse, this);
  Bind(wxEVT_SET_FOCUS, &TexturePreview::OnFocus, this);
  Bind(wxEVT_KILL_FOCUS, &TexturePreview::OnFocus, this);
  // Resizing has paused: replace a quick scale with the smooth one.
  m_resizeSettle.Bind(wxEVT_TIMER, [this](wxTimerEvent &) {
    if (!m_scaled.isNull() && !m_scaledSmooth)
    {
      m_scaled = QImage();
      m_scaledSize = QSize();
      dropComposed();
      Refresh();
    }
  });
}

void TexturePreview::setImage(const QImage & image, const QSize & textureSize)
{
  m_source = image.format() == QImage::Format_ARGB32 ? image : image.convertToFormat(QImage::Format_ARGB32);
  m_textureSize = textureSize.isValid() && textureSize != m_source.size() ? textureSize : QSize();
  m_viewImageValid = false;
  m_viewImage = QImage();
  m_scaled = QImage();
  m_scaledSize = QSize();
  m_detailAskedFor = 0;
  dropComposed();
  m_title.clear();
  m_detail.clear();
  m_hint.clear();
  m_note.clear();
  m_reportPaint = true;
  Refresh();
}

void TexturePreview::setMessage(const wxString & title, const wxString & detail, const wxString & hint)
{
  m_source = QImage();
  m_textureSize = QSize();
  m_viewImage = QImage();
  m_viewImageValid = false;
  m_scaled = QImage();
  m_scaledSize = QSize();
  dropComposed();
  m_title = title;
  m_detail = detail;
  m_hint = hint;
  m_note.clear();
  Refresh();
}

void TexturePreview::setNote(const wxString & note)
{
  if (note == m_note)
    return;
  m_note = note;
  Refresh();
}

void TexturePreview::setAlphaView(AlphaView view)
{
  if (view == m_alphaView)
    return;
  m_alphaView = view;
  m_scaled = QImage();
  m_scaledSize = QSize();
  dropComposed();
  Refresh();
}

void TexturePreview::setBackground(Background background)
{
  if (background == m_background)
    return;
  m_background = background;
  dropComposed();
  Refresh();
}

void TexturePreview::releaseCaches()
{
  m_viewImage = QImage();
  m_viewImageValid = false;
  m_scaled = QImage();
  m_scaledSize = QSize();
  dropComposed();
}

void TexturePreview::dropComposed()
{
  m_composed = wxBitmap();
  m_composedSize = QSize();
  m_composedValid = false;
}

QSize TexturePreview::textureSize() const
{
  return m_textureSize.isValid() ? m_textureSize : m_source.size();
}

const QImage & TexturePreview::viewSource()
{
  if (m_alphaView == AlphaView::On)
    return m_source;
  if (!m_viewImageValid || m_viewImageFor != m_alphaView)
  {
    m_viewImage = convertForView(m_source, m_alphaView);
    m_viewImageFor = m_alphaView;
    m_viewImageValid = true;
  }
  return m_viewImage;
}

double TexturePreview::scaleFor(const QSize & texture, const wxSize & area) const
{
  if (texture.isEmpty())
    return 1.0;
  const int margin = FromDIP(Margin);
  const double w = std::max(1, area.x - 2 * margin), h = std::max(1, area.y - 2 * margin);
  const double fit = std::min(w / texture.width(), h / texture.height());
  // Larger than the area: down to fit it.
  if (fit < 1)
    return std::max(MinScale, fit);
  // Smaller: a whole number of screen pixels per texel, so every texel stays the same size; no more
  // than MaxEnlargement device-independent pixels each (at 150% display scaling, 3 screen pixels); and
  // only while the longer side stays within EnlargeUpTo device-independent pixels.
  const double dpi = GetDPIScaleFactor();
  const int longest = std::max(texture.width(), texture.height());
  int times = std::min((int)std::floor(m_maxEnlargement * dpi + 1e-6), (int)std::floor(fit));
  times = std::min(times, (int)std::floor(m_enlargeUpTo * dpi / longest + 1e-6));
  return std::max(1, times);
}

double TexturePreview::displayScale() const
{
  return m_source.isNull() ? 1.0 : scaleFor(textureSize(), GetClientSize());
}

QSize TexturePreview::displaySizeFor(const QSize & texture) const
{
  const double scale = scaleFor(texture, GetClientSize());
  return QSize(std::max(1, (int)std::lround(texture.width() * scale)), std::max(1, (int)std::lround(texture.height() * scale)));
}

QRect TexturePreview::displayRect() const
{
  if (m_source.isNull())
    return QRect();
  const wxSize size = GetClientSize();
  const QSize shown = displaySizeFor(textureSize());
  return QRect((size.x - shown.width()) / 2, (size.y - shown.height()) / 2, shown.width(), shown.height());
}

void TexturePreview::compose(const QRect & rect)
{
  const QSize size = rect.size();
  QImage picture(size, QImage::Format_RGB32);
  QPainter painter(&picture);
  // The background under the image. Checker cells are anchored to the image's corner.
  if (m_background == Background::Checkerboard)
  {
    const int cell = std::max(2, FromDIP(8));
    const QColor a = QColor::fromRgb(rgbOf(backgroundColour(m_background, false)));
    const QColor b = QColor::fromRgb(rgbOf(backgroundColour(m_background, true)));
    picture.fill(a);
    for (int cy = 0; cy * cell < size.height(); cy++)
      for (int cx = (cy & 1) ? 0 : 1; cx * cell < size.width(); cx += 2)
        painter.fillRect(QRect(cx * cell, cy * cell, cell, cell), b);
  }
  else
    picture.fill(QColor::fromRgb(rgbOf(backgroundColour(m_background))));

  const bool whole = size.width() % m_source.width() == 0 && size.height() % m_source.height() == 0 &&
                     size.width() / m_source.width() == size.height() / m_source.height();
  if (whole)
  {
    // A whole number of pixels per texel (1:1 included): each texel drawn as an exact square.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(QRect(QPoint(0, 0), size), viewSource());
  }
  else
  {
    // Reduced (from the texture or from a smaller level of it): scaled smoothly once per size. While a
    // drag keeps resizing a large image, a nearest scale (its cost is the drawn size rather than the
    // image's) until the drag pauses.
    if (m_scaled.isNull() || m_scaledSize != size)
    {
      const bool quick = m_resizeSettle.IsRunning() && wxGetMouseState().LeftIsDown() &&
                         (qint64)m_source.width() * m_source.height() > 1024 * 1024;
      m_scaled = viewSource().scaled(size, Qt::IgnoreAspectRatio, quick ? Qt::FastTransformation : Qt::SmoothTransformation);
      m_scaledSize = size;
      m_scaledSmooth = !quick;
    }
    painter.drawImage(QPoint(0, 0), m_scaled);
  }
  painter.end();
  m_composed = toBitmap(picture);
  m_composedSize = size;
  m_composedValid = true;
}

void TexturePreview::OnPaint(wxPaintEvent &)
{
  QElapsedTimer paintTimer;
  paintTimer.start();
  wxAutoBufferedPaintDC dc(this);
  const wxSize size = GetClientSize();
  if (size.x <= 0 || size.y <= 0)
    return;
  dc.SetBackground(wxBrush(Surround));
  dc.Clear();

  if (!m_source.isNull())
  {
    const QRect rect = displayRect();
    // A smaller level shown larger than it is: ask for a larger one (drawn enlarged meanwhile).
    const int longest = std::max(rect.width(), rect.height());
    if (m_textureSize.isValid() && longest > std::max(m_source.width(), m_source.height()) && longest != m_detailAskedFor &&
        onNeedsDetail)
    {
      m_detailAskedFor = longest;
      CallAfter([this, longest]() {
        if (onNeedsDetail)
          onNeedsDetail(longest);
      });
    }
    if (!m_composedValid || m_composedSize != rect.size())
      compose(rect);
    dc.DrawBitmap(m_composed, rect.x(), rect.y());
    // A hairline around the image, so its extent shows on any background.
    dc.SetPen(wxPen(Hairline));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(rect.x() - 1, rect.y() - 1, rect.width() + 2, rect.height() + 2);
  }
  else if (!m_title.IsEmpty())
  {
    // The message: a title and wrapped detail lines, centred.
    wxFont font = GetFont();
    wxFont bold = font.Bold();
    bold.SetPointSize(font.GetPointSize() + 2);
    const int width = std::min(size.x - FromDIP(48), FromDIP(520));
    wxArrayString detailLines;
    dc.SetFont(font);
    for (const wxString & part : { m_detail, m_hint })
    {
      if (part.IsEmpty())
        continue;
      wxString line;
      for (const wxString & word : wxSplit(part, ' ', '\0'))
      {
        const wxString candidate = line.IsEmpty() ? word : line + wxT(" ") + word;
        if (!line.IsEmpty() && dc.GetTextExtent(candidate).x > width)
        {
          detailLines.Add(line);
          line = word;
        }
        else
          line = candidate;
      }
      detailLines.Add(line);
      detailLines.Add(wxEmptyString);
    }
    dc.SetFont(bold);
    const int titleH = dc.GetTextExtent(m_title).y;
    dc.SetFont(font);
    const int lineH = dc.GetCharHeight();
    const int total = titleH + (detailLines.IsEmpty() ? 0 : FromDIP(8) + lineH * (int)detailLines.size());
    int y = (size.y - total) / 2;
    dc.SetFont(bold);
    dc.SetTextForeground(SurroundText);
    dc.DrawText(m_title, (size.x - dc.GetTextExtent(m_title).x) / 2, y);
    y += titleH + FromDIP(8);
    dc.SetFont(font);
    dc.SetTextForeground(SurroundDetail);
    for (const wxString & line : detailLines)
    {
      dc.DrawText(line, (size.x - dc.GetTextExtent(line).x) / 2, y);
      y += lineH;
    }
  }

  // The note: small, in the bottom-left corner, over whatever is shown.
  if (!m_note.IsEmpty())
  {
    dc.SetFont(GetFont());
    const wxSize text = dc.GetTextExtent(m_note);
    const int pad = FromDIP(6), inset = FromDIP(12);
    const wxRect box(inset, size.y - inset - text.y - 2 * pad, text.x + 2 * pad, text.y + 2 * pad);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(wxColour(40, 40, 40)));
    dc.DrawRoundedRectangle(box, FromDIP(4));
    dc.SetTextForeground(SurroundText);
    dc.DrawText(m_note, box.x + pad, box.y + pad);
  }

  if (HasFocus())
  {
    dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT), FromDIP(2)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(wxRect(size).Deflate(FromDIP(1)));
  }

  if (m_reportPaint && !m_source.isNull())
  {
    m_reportPaint = false;
    if (onImagePainted)
      onImagePainted(paintTimer.nsecsElapsed() / 1e6);
  }
}

void TexturePreview::OnSize(wxSizeEvent & event)
{
  // The size follows the area. While the mouse drags a sash or the window's edge, a large image is
  // composed roughly until the drag pauses (compose); a panel shown or hidden is smooth at once.
  m_resizeSettle.StartOnce(150);
  if (!m_source.isNull() && displayRect().size() != m_composedSize)
    dropComposed();
  Refresh();
  event.Skip();
}

void TexturePreview::OnMouse(wxMouseEvent & event)
{
  // The wheel stops here: a texture has no zoom, and the wheel must not reach the viewport hidden
  // behind the texture view either.
  if (event.GetEventType() == wxEVT_MOUSEWHEEL)
    return;
  // A click gives the preview the keyboard, for the alpha keys (TextureView::OnCharHook).
  if (event.GetEventType() == wxEVT_LEFT_DOWN)
    SetFocus();
  event.Skip();
}

void TexturePreview::OnFocus(wxFocusEvent & event)
{
  Refresh();
  event.Skip();
}
