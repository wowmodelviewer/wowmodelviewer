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
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

/*
 * ExportJobManager.h
 *
 *  Runs FBX exports in a SEPARATE PROCESS so the FBX SDK (which is not thread-safe
 *  and mutates process-global state) can never freeze, corrupt, or crash the main
 *  application. The manager:
 *    - spawns the existing wowmodelviewer.exe in its headless export mode,
 *    - streams its WMVEXPORT-PROGRESS stdout into a modeless progress dialog
 *      (stage + animation + percent) while the main UI stays fully interactive,
 *    - supports cancellation (kills the whole child process tree),
 *    - watches for a hung child (no progress for too long) and kills it,
 *    - reads the child's "<out>.status" sidecar to report success/failure,
 *    - writes a detailed "<out>.export.log" of everything the child emitted,
 *    - refuses a second, duplicate job for the same asset+target, and
 *    - cleans up any temporary character file it created for the export.
 *
 *  All of this runs on the wx main thread via a poll timer and the child's
 *  wxProcess termination event, so there are no threading concerns here.
 */

#ifndef _EXPORTJOBMANAGER_H_
#define _EXPORTJOBMANAGER_H_

#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/process.h>
#include <wx/timer.h>
#include <wx/ffile.h>
#include <wx/datetime.h>

#include <list>

class ModelViewer;
class ExportProgressDialog;

// One running (or just-finished) export. Owned by ExportJobManager via pointer so the
// non-copyable members (wxFFile, wxProcess*) are never copied.
struct ExportJob
{
  wxString   assetKey;        // dedupe identity: asset descriptor + output path
  wxString   assetLabel;      // human label for the dialog
  wxString   outPath;         // target .fbx
  wxString   statusPath;      // outPath + ".status" (written by the child; authoritative)
  wxString   logPath;         // outPath + ".export.log"
  wxString   lockPath;        // outPath + ".lock"
  wxString   tempCharPath;    // temp .chr to delete on finish (empty if none)

  long       pid;
  wxProcess* process;
  ExportProgressDialog* dialog;

  wxFFile    logFile;         // detailed per-export log (child stdout/stderr)
  wxString   lineBuf;         // partial-line accumulator for the child's stdout

  wxString   stage;           // last STAGE reported
  int        totalClips;
  int        clipNum;
  int        percent;

  wxDateTime lastProgress;    // for the hang watchdog
  bool       hungFlagged;
  bool       cancelled;
  bool       terminated;      // OnTerminate already delivered
  int        exitCode;

  ExportJob()
    : pid(0), process(NULL), dialog(NULL),
      totalClips(0), clipNum(0), percent(0),
      hungFlagged(false), cancelled(false), terminated(false), exitCode(0)
  {}
};

class ExportJobManager : public wxEvtHandler
{
  public:
    // Everything the manager needs to spawn and track one export. The caller
    // (ModelViewer::OnExport) builds the asset descriptor and the option flags.
    struct Request
    {
      wxString assetArgs;   // child CLI for the asset: "\"x.chr\"", "-mo \"path\"", "-npc id:disp"
      wxString assetLabel;  // shown in the progress dialog
      wxString outPath;     // target .fbx chosen by the user
      wxString build;       // current loaded build version (-build); may be empty
      bool     mesh;
      bool     skeleton;
      bool     skinning;
      bool     animation;
      bool     component;   // opt-in raw/node-based item-component export (-fbxcomponent)
      wxString clipsCsv;    // comma-separated clip indices (empty => no clips)
      wxString tempCharPath;// temp .chr to delete when done (empty if none)

      Request() : mesh(true), skeleton(true), skinning(true), animation(true), component(false) {}
    };

    explicit ExportJobManager(ModelViewer * owner);
    ~ExportJobManager();

    // Starts an export. Returns false (and shows a message) if a job for the same
    // asset+target is already running, or if the child could not be launched.
    bool startExport(const Request & req);

    // Called by the per-process wxProcess subclass when the child exits.
    void onProcessTerminated(ExportJob * job, int exitCode);

  private:
    void onPoll(wxTimerEvent & event);
    void drainOutput(ExportJob * job);
    void handleLine(ExportJob * job, const wxString & line);
    void updatePercentFromStage(ExportJob * job);
    void finalize(ExportJob * job);
    void killChild(ExportJob * job);
    wxString computeAssetKey(const Request & req) const;
    ExportJob * findActiveByKey(const wxString & key) const;

    ModelViewer *          m_owner;
    wxTimer                m_pollTimer;
    std::list<ExportJob*>  m_jobs;
};

#endif /* _EXPORTJOBMANAGER_H_ */
