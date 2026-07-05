/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

#include "ImageSequenceExporter.h"

#include "modelviewer.h"
#include "modelcanvas.h"
#include "ExportProgressDialog.h"
#include "video.h"
#include "WoWModel.h"
#include "AnimManager.h"
#include "logger/Logger.h"

#include <wx/filename.h>
#include <wx/utils.h>
#include <cmath>

namespace
{
  const char * fmtExt(int f)
  {
    switch (f) { case ImageSequenceExporter::FMT_JPG: return "jpg";
                 case ImageSequenceExporter::FMT_EXR: return "exr";
                 default: return "png"; }
  }

  WoWModel * mdlOf(ModelCanvas * c)
  {
    return c ? const_cast<WoWModel*>(c->model()) : nullptr;
  }
}

ImageSequenceExporter::ImageSequenceExporter(ModelViewer * owner)
  : m_owner(owner), m_canvas(owner ? owner->canvas : nullptr), m_dialog(nullptr),
    m_frameIdx(0), m_totalFrames(0), m_running(false),
    m_savedFrame(0), m_savedPaused(false)
{
  m_timer.SetOwner(this);
  Bind(wxEVT_TIMER, &ImageSequenceExporter::onTick, this);
}

ImageSequenceExporter::~ImageSequenceExporter()
{
  if (m_running)
    finish(true, wxEmptyString);
}

wxString ImageSequenceExporter::framePath(const Settings & s, int i)
{
  const wxString num = wxString::Format(wxT("%0*d"), s.padding, s.startNumber + i);
  return s.folder + wxFileName::GetPathSeparator() + s.prefix + wxT("_") + num + wxT(".") + wxString::FromUTF8(fmtExt(s.format));
}

int ImageSequenceExporter::countExistingFrames(const Settings & s)
{
  // Cheap pre-flight: a sequence reuses one naming scheme, so if frame 0 already exists the rest
  // almost certainly do too. Used only to decide whether to warn about overwrite.
  return wxFileName::FileExists(framePath(s, 0)) ? 1 : 0;
}

bool ImageSequenceExporter::start(const Settings & s)
{
  if (m_running)
  {
    wxMessageBox(wxT("An image-sequence export is already running."), wxT("Export"), wxOK | wxICON_INFORMATION, m_owner);
    return false;
  }

  m_canvas = m_owner ? m_owner->canvas : nullptr;
  WoWModel * mdl = mdlOf(m_canvas);
  if (!mdl || !mdl->animManager)
  {
    wxMessageBox(wxT("Load a model (with an animation) before exporting an image sequence."),
                 wxT("Export Error"), wxOK | wxICON_ERROR, m_owner);
    return false;
  }

  m_s = s;

  // ---- validation ----
  if (m_s.width < 32 || m_s.height < 32 || m_s.width > 8192 || m_s.height > 8192)
  {
    wxMessageBox(wxT("Resolution must be between 32 and 8192 on each axis."), wxT("Export Error"), wxOK | wxICON_ERROR, m_owner);
    return false;
  }
  if (m_s.fps <= 0.0 || m_s.fps > 240.0)
  {
    wxMessageBox(wxT("Frame rate must be between 0 and 240 fps."), wxT("Export Error"), wxOK | wxICON_ERROR, m_owner);
    return false;
  }
  if (m_s.rangeEndMs < m_s.rangeStartMs)
  {
    wxMessageBox(wxT("The frame range is invalid (end is before start)."), wxT("Export Error"), wxOK | wxICON_ERROR, m_owner);
    return false;
  }
  if (m_s.padding < 1 || m_s.padding > 9) m_s.padding = 4;

  // ---- output folder: create if needed, verify writable ----
  if (!wxFileName::DirExists(m_s.folder))
  {
    if (!wxFileName::Mkdir(m_s.folder, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL))
    {
      wxMessageBox(wxT("Could not create the output folder:\n") + m_s.folder, wxT("Export Error"), wxOK | wxICON_ERROR, m_owner);
      return false;
    }
  }
  if (!wxFileName::IsDirWritable(m_s.folder))
  {
    wxMessageBox(wxT("The output folder is not writable:\n") + m_s.folder, wxT("Export Error"), wxOK | wxICON_ERROR, m_owner);
    return false;
  }

  // ---- frame count ----
  const double stepMs = 1000.0 / m_s.fps;
  m_totalFrames = (int)std::floor((double)(m_s.rangeEndMs - m_s.rangeStartMs) / stepMs) + 1;
  if (m_totalFrames < 1) m_totalFrames = 1;

  // ---- overwrite guard ----
  if (!m_s.overwrite && countExistingFrames(m_s) > 0)
  {
    const int r = wxMessageBox(wxT("Frames with this name already exist in the output folder.\n\nOverwrite them?"),
                               wxT("Overwrite?"), wxYES_NO | wxICON_QUESTION, m_owner);
    if (r != wxYES)
      return false;
    m_s.overwrite = true;
  }

  // ---- disk-space sanity (best-effort, warn only) ----
  {
    wxLongLong freeBytes;
    if (wxGetDiskSpace(m_s.folder, NULL, &freeBytes))
    {
      const wxLongLong perFrame = (wxLongLong)m_s.width * m_s.height * (m_s.format == FMT_EXR ? 16 : 4);
      const wxLongLong need = perFrame * m_totalFrames;
      if (freeBytes < need)
      {
        const int r = wxMessageBox(wxString::Format(wxT("This export may need up to ~%lld MB but only ~%lld MB is free.\n\nContinue anyway?"),
                                                    (need / (1024*1024)).GetValue(), (freeBytes / (1024*1024)).GetValue()),
                                   wxT("Low disk space"), wxYES_NO | wxICON_WARNING, m_owner);
        if (r != wxYES)
          return false;
      }
    }
  }

  // ---- snapshot viewport state (export must be non-destructive) ----
  // (CaptureSequenceFrame manages the background/alpha itself via a two-pass matte, saving and
  // restoring video.useMasking per frame, so we don't touch it here.)
  m_savedFrame   = (unsigned)mdl->animManager->GetFrame();
  m_savedPaused  = mdl->animManager->IsPaused();

  mdl->animManager->Pause(true);          // we drive time deterministically via SetFrame

  // ---- detailed log ----
  const wxString logPath = m_s.folder + wxFileName::GetPathSeparator() + m_s.prefix + wxT("_export.log");
  if (m_log.Open(logPath, wxT("w")))
  {
    m_log.Write(wxT("WoW Model Viewer image-sequence export\n"));
    m_log.Write(wxString::Format(wxT("Output : %s\\%s_%s.%s\n"), m_s.folder, m_s.prefix,
                                 wxString('0', m_s.padding), wxString::FromUTF8(fmtExt(m_s.format))));
    m_log.Write(wxString::Format(wxT("Frames : %d  (%.3f fps, range %u..%u ms)\n"), m_totalFrames, m_s.fps, m_s.rangeStartMs, m_s.rangeEndMs));
    m_log.Write(wxString::Format(wxT("Size   : %dx%d  transparent=%d  colorspace=sRGB%s\n"),
                                 m_s.width, m_s.height, (int)m_s.transparent, (m_s.format == FMT_EXR ? wxT(" (EXR=linear float)") : wxT(""))));
    m_log.Write(wxT("----------------------------------------\n"));
    m_log.Flush();
  }

  // ---- progress UI + go ----
  m_dialog = new ExportProgressDialog(m_owner, wxEmptyString, wxT("Image Sequence"));
  m_dialog->setStage(wxT("starting..."));
  m_dialog->Show();

  m_frameIdx = 0;
  m_running = true;
  m_timer.Start(1); // ~one frame per event-loop tick; handler returns each frame so the UI pumps
  return true;
}

void ImageSequenceExporter::onTick(wxTimerEvent & WXUNUSED(event))
{
  if (!m_running)
    return;

  if (m_dialog && m_dialog->cancelRequested())
  {
    finish(true, wxEmptyString);
    return;
  }
  if (m_frameIdx >= m_totalFrames)
  {
    finish(false, wxEmptyString);
    return;
  }

  WoWModel * mdl = mdlOf(m_canvas);
  if (!mdl || !mdl->animManager)
  {
    finish(false, wxT("The model was unloaded during export."));
    return;
  }

  // Absolute animation time for this frame (forward-integrated by SetFrame from the previous frame).
  const double stepMs = 1000.0 / m_s.fps;
  unsigned t = m_s.rangeStartMs + (unsigned)std::lround(m_frameIdx * stepMs);
  if (t > m_s.rangeEndMs) t = m_s.rangeEndMs;
  mdl->animManager->SetFrame(t);

  const wxString path = framePath(m_s, m_frameIdx);
  const bool ok = m_canvas->CaptureSequenceFrame(path, m_s.width, m_s.height, m_s.format, m_s.transparent);

  if (m_log.IsOpened())
  {
    m_log.Write(wxString::Format(wxT("frame %04d  t=%u ms  -> %s  %s\n"),
                                 m_frameIdx, t, wxFileName(path).GetFullName(), ok ? wxT("ok") : wxT("FAILED")));
    m_log.Flush();
  }

  if (!ok)
  {
    finish(false, wxT("Failed to write a frame (see the export log)."));
    return;
  }

  m_frameIdx++;

  if (m_dialog)
  {
    m_dialog->setStage(wxString::Format(wxT("rendering frame %d of %d"), m_frameIdx, m_totalFrames));
    m_dialog->setClip(m_frameIdx, m_totalFrames, wxFileName(path).GetFullName());
    m_dialog->setPercent((int)(100.0 * m_frameIdx / m_totalFrames));
  }
}

void ImageSequenceExporter::restoreState()
{
  WoWModel * mdl = mdlOf(m_canvas);
  if (mdl && mdl->animManager)
  {
    mdl->animManager->SetFrame(m_savedFrame);
    if (!m_savedPaused)
      mdl->animManager->Play();
  }
  if (m_canvas)
    m_canvas->Refresh();
}

void ImageSequenceExporter::finish(bool cancelled, const wxString & err)
{
  m_timer.Stop();
  m_running = false;
  restoreState();

  if (m_log.IsOpened())
  {
    m_log.Write(wxString::Format(wxT("----------------------------------------\n%s (%d/%d frames)\n"),
                                 cancelled ? wxT("CANCELLED") : (err.IsEmpty() ? wxT("DONE") : wxT("ERROR: ") + err),
                                 m_frameIdx, m_totalFrames));
    m_log.Close();
  }

  if (m_dialog) { m_dialog->Destroy(); m_dialog = nullptr; }

  if (cancelled)
  {
    if (m_owner) m_owner->SetStatusText(wxT("Image-sequence export cancelled."));
  }
  else if (!err.IsEmpty())
  {
    wxMessageBox(wxT("Image-sequence export failed.\n\n") + err, wxT("Export Error"), wxOK | wxICON_ERROR, m_owner);
  }
  else
  {
    LOG_INFO << "[imgseq] exported" << m_frameIdx << "frames to" << (const char*)m_s.folder.mb_str();
    wxMessageBox(wxString::Format(wxT("Exported %d frames to:\n%s"), m_frameIdx, m_s.folder),
                 wxT("Export done"), wxOK | wxICON_INFORMATION, m_owner);
    if (m_s.openFolderWhenDone)
      wxLaunchDefaultApplication(m_s.folder);
  }
}

void ImageSequenceExporter::log(const wxString & line)
{
  if (m_log.IsOpened()) { m_log.Write(line + wxT("\n")); m_log.Flush(); }
}
