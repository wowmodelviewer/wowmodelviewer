/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

/*
 * ImageSequenceExporter.h
 *
 *  Renders the live, fully-customised model to a numbered PNG/JPG/EXR frame sequence for import
 *  into After Effects / Premiere / DaVinci Resolve. OpenGL + the single GL context are thread-
 *  affine, so rendering MUST stay on the main thread; to keep the UI responsive this exporter is a
 *  wxTimer-driven state machine that renders exactly ONE frame per tick (the handler returns after
 *  each frame, so the event loop keeps pumping -- progress updates, Cancel, and the live viewport
 *  all stay alive). It snapshots and restores the animation time + masking state so the export is
 *  non-destructive to the viewport.
 */

#ifndef _IMAGESEQUENCEEXPORTER_H_
#define _IMAGESEQUENCEEXPORTER_H_

#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/timer.h>
#include <wx/ffile.h>

class ModelViewer;
class ModelCanvas;
class ExportProgressDialog;

class ImageSequenceExporter : public wxEvtHandler
{
  public:
    enum Format { FMT_PNG = 0, FMT_JPG = 1, FMT_EXR = 2 };

    struct Settings
    {
      wxString folder;       // output directory
      wxString prefix;       // e.g. "Bloodelf_Walk" -> Bloodelf_Walk_0001.png
      int      format;       // Format enum
      int      padding;      // frame-number digits (default 4)
      int      startNumber;  // first frame number used in filenames (default 1)
      int      width, height;
      double   fps;          // frames per second to sample at
      unsigned rangeStartMs; // animation time of the first exported frame
      unsigned rangeEndMs;   // animation time of the last exported frame (inclusive)
      bool     transparent;  // alpha background (PNG/EXR)
      bool     openFolderWhenDone;
      bool     overwrite;    // proceed even if files already exist

      Settings()
        : format(FMT_PNG), padding(4), startNumber(1), width(1920), height(1080),
          fps(30.0), rangeStartMs(0), rangeEndMs(0), transparent(true),
          openFolderWhenDone(true), overwrite(false) {}
    };

    explicit ImageSequenceExporter(ModelViewer * owner);
    ~ImageSequenceExporter();

    // Validates settings, snapshots state, and starts the per-frame timer. Shows an error and
    // returns false if validation fails or an export is already running.
    bool start(const Settings & s);
    bool isRunning() const { return m_running; }

    // True if `s` would overwrite existing frames (so the caller can warn). Also returns the count.
    static int countExistingFrames(const Settings & s);
    // Builds the absolute path for frame index i (0-based).
    static wxString framePath(const Settings & s, int i);

  private:
    void onTick(wxTimerEvent & event);
    void finish(bool cancelled, const wxString & err);
    void restoreState();
    void log(const wxString & line);

    ModelViewer * m_owner;
    ModelCanvas * m_canvas;
    Settings      m_s;

    wxTimer               m_timer;
    ExportProgressDialog* m_dialog;
    wxFFile               m_log;

    int  m_frameIdx;
    int  m_totalFrames;
    bool m_running;

    // snapshot of viewport state to restore afterwards
    unsigned m_savedFrame;
    bool     m_savedPaused;
};

#endif /* _IMAGESEQUENCEEXPORTER_H_ */
