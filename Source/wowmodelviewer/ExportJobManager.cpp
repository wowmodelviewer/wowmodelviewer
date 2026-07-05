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

#include "ExportJobManager.h"
#include "ExportProgressDialog.h"
#include "modelviewer.h"

#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/utils.h>   // wxKILL_CHILDREN
#include <wx/txtstrm.h>

#include "logger/Logger.h"

namespace
{
  // Poll the child's stdout this often (ms) and re-evaluate the hang watchdog.
  const int POLL_INTERVAL_MS = 150;

  // No progress line for this long => warn in the dialog.
  const int HANG_WARN_SECS = 45;
  // No progress line for this long => assume the child is wedged and kill it, so the
  // main application can never be held hostage by a stuck exporter.
  const int HANG_KILL_SECS = 240;

  // A lock sidecar older than this is treated as stale (left by a crashed parent) and
  // ignored, so a single crash can't permanently block re-exporting a file.
  const int LOCK_STALE_HOURS = 6;

  // Map a coarse export stage to a progress percentage. Animation clips fill the band
  // between SKIN and WRITE proportionally (handled in handleLine).
  int stagePercent(const wxString & s)
  {
    if (s == wxT("START"))     return 2;
    if (s == wxT("SKELETON"))  return 12;
    if (s == wxT("MESH"))      return 35;
    if (s == wxT("MATERIALS")) return 50;
    if (s == wxT("SKIN"))      return 62;
    if (s == wxT("WRITE"))     return 95;
    if (s == wxT("DONE"))      return 100;
    return -1;
  }
}

// wxProcess subclass that routes the child's termination back to the manager.
class ExportProcess : public wxProcess
{
  public:
    ExportProcess(ExportJobManager * mgr, ExportJob * job)
      : wxProcess(NULL), m_mgr(mgr), m_job(job)
    {
      Redirect();
    }

    void OnTerminate(int WXUNUSED(pid), int status) override
    {
      m_mgr->onProcessTerminated(m_job, status);
      // wx deletes this wxProcess automatically after OnTerminate returns.
    }

  private:
    ExportJobManager * m_mgr;
    ExportJob        * m_job;
};

ExportJobManager::ExportJobManager(ModelViewer * owner)
  : m_owner(owner)
{
  m_pollTimer.SetOwner(this);
  Bind(wxEVT_TIMER, &ExportJobManager::onPoll, this);
}

ExportJobManager::~ExportJobManager()
{
  // Stop the timer; any still-running children are detached (the OS reaps them). We do
  // not force-kill on shutdown -- a finished-but-unreaped export should still produce a file.
  m_pollTimer.Stop();
  for (std::list<ExportJob*>::iterator it = m_jobs.begin(); it != m_jobs.end(); ++it)
    delete *it;
  m_jobs.clear();
}

wxString ExportJobManager::computeAssetKey(const Request & req) const
{
  // Identity for duplicate detection: what is being exported + where it's going.
  return req.assetArgs + wxT("=>") + req.outPath;
}

ExportJob * ExportJobManager::findActiveByKey(const wxString & key) const
{
  for (std::list<ExportJob*>::const_iterator it = m_jobs.begin(); it != m_jobs.end(); ++it)
    if (!(*it)->terminated && (*it)->assetKey == key)
      return *it;
  return NULL;
}

bool ExportJobManager::startExport(const Request & req)
{
  const wxString assetKey = computeAssetKey(req);

  // 1) Same-process duplicate guard.
  if (findActiveByKey(assetKey))
  {
    wxMessageBox(wxT("An export for this model and target file is already in progress.\n\n"
                     "Wait for it to finish (or cancel it) before exporting the same thing again."),
                 wxT("Export already running"), wxOK | wxICON_INFORMATION, m_owner);
    return false;
  }

  const wxString lockPath = req.outPath + wxT(".lock");

  // 2) Cross-instance duplicate guard: a fresh lock sidecar means another WMV (or a
  //    previous run) is writing this same file. Stale locks (older than LOCK_STALE_HOURS,
  //    e.g. left by a crashed parent) are ignored so they can't block forever.
  if (wxFileName::FileExists(lockPath))
  {
    wxDateTime mtime;
    wxFileName lf(lockPath);
    bool fresh = lf.GetTimes(NULL, &mtime, NULL) &&
                 mtime.IsValid() &&
                 (wxDateTime::Now() - mtime).GetHours() < LOCK_STALE_HOURS;
    if (fresh)
    {
      wxMessageBox(wxT("Another export is already writing to this file.\n\n"
                       "Choose a different output file, or wait for the other export to finish."),
                   wxT("Export already running"), wxOK | wxICON_INFORMATION, m_owner);
      return false;
    }
    wxRemoveFile(lockPath);
  }

  // 3) Remove a stale status sidecar so we never read an old result for the new run.
  const wxString statusPath = req.outPath + wxT(".status");
  if (wxFileName::FileExists(statusPath))
    wxRemoveFile(statusPath);

  // 4) Build the child command line. assetArgs is already correctly quoted by the caller.
  const wxString exePath = wxStandardPaths::Get().GetExecutablePath();
  wxString cmd;
  cmd << wxT("\"") << exePath << wxT("\" ")
      << req.assetArgs
      << wxT(" -fbxexport \"") << req.outPath << wxT("\"")
      << wxT(" -fbxmesh ") << (req.mesh ? 1 : 0)
      << wxT(" -fbxskel ") << (req.skeleton ? 1 : 0)
      << wxT(" -fbxskin ") << (req.skinning ? 1 : 0)
      << wxT(" -fbxanim ") << (req.animation ? 1 : 0);
  if (req.component)
    cmd << wxT(" -fbxcomponent");
  if (!req.clipsCsv.IsEmpty())
    cmd << wxT(" -fbxclips ") << req.clipsCsv;
  if (!req.build.IsEmpty())
    cmd << wxT(" -build \"") << req.build << wxT("\"");

  // 5) Prepare the job + detailed log.
  ExportJob * job = new ExportJob();
  job->assetKey     = assetKey;
  job->assetLabel   = req.assetLabel;
  job->outPath      = req.outPath;
  job->statusPath   = statusPath;
  job->logPath      = req.outPath + wxT(".export.log");
  job->lockPath     = lockPath;
  job->tempCharPath = req.tempCharPath;
  job->lastProgress = wxDateTime::Now();

  if (job->logFile.Open(job->logPath, wxT("w")))
  {
    job->logFile.Write(wxT("WoW Model Viewer FBX export log\n"));
    job->logFile.Write(wxT("Asset : ") + req.assetLabel + wxT("\n"));
    job->logFile.Write(wxT("Output: ") + req.outPath + wxT("\n"));
    job->logFile.Write(wxT("Build : ") + req.build + wxT("\n"));
    job->logFile.Write(wxT("Command: ") + cmd + wxT("\n"));
    job->logFile.Write(wxT("----------------------------------------\n"));
    job->logFile.Flush();
  }

  // 6) Create the lock sidecar (best-effort).
  {
    wxFFile lock;
    if (lock.Open(lockPath, wxT("w")))
    {
      lock.Write(wxString::Format(wxT("%ld\n"), (long)wxGetProcessId()));
      lock.Close();
    }
  }

  // 7) Spawn the child, redirected, asynchronously.
  ExportProcess * proc = new ExportProcess(this, job);
  job->process = proc;
  const long pid = wxExecute(cmd, wxEXEC_ASYNC, proc);
  if (pid == 0)
  {
    if (job->logFile.IsOpened())
    {
      job->logFile.Write(wxT("\n[manager] FAILED to launch export process.\n"));
      job->logFile.Close();
    }
    wxRemoveFile(lockPath);
    delete proc;       // not associated with a running process
    delete job;
    wxMessageBox(wxT("Could not start the export process."),
                 wxT("Export Error"), wxOK | wxICON_ERROR, m_owner);
    return false;
  }
  job->pid = pid;
  LOG_INFO << "[export] launched child pid" << pid << "->" << (const char*)req.outPath.mb_str();

  // 8) Show the modeless progress dialog and start polling.
  job->dialog = new ExportProgressDialog(m_owner, req.assetLabel);
  job->dialog->Show();

  m_jobs.push_back(job);
  if (!m_pollTimer.IsRunning())
    m_pollTimer.Start(POLL_INTERVAL_MS);

  return true;
}

void ExportJobManager::onPoll(wxTimerEvent & WXUNUSED(event))
{
  // Iterate over a copy-safe snapshot: finalize() may erase from m_jobs.
  std::list<ExportJob*> snapshot(m_jobs);
  for (std::list<ExportJob*>::iterator it = snapshot.begin(); it != snapshot.end(); ++it)
  {
    ExportJob * job = *it;
    if (job->terminated)
      continue;

    drainOutput(job);

    // User pressed Cancel in the dialog -> kill the child; OnTerminate finalizes.
    if (job->dialog && job->dialog->cancelRequested() && !job->cancelled)
    {
      job->cancelled = true;
      if (job->logFile.IsOpened())
        job->logFile.Write(wxT("\n[manager] cancelled by user; terminating child.\n"));
      killChild(job);
      continue;
    }

    // Hang watchdog.
    const wxTimeSpan idle = wxDateTime::Now() - job->lastProgress;
    if (idle.GetSeconds() >= HANG_KILL_SECS)
    {
      if (job->logFile.IsOpened())
        job->logFile.Write(wxT("\n[manager] no progress for too long; assuming hung, terminating child.\n"));
      LOG_WARNING << "[export] child pid" << job->pid << "hung; killing";
      killChild(job);
    }
    else if (idle.GetSeconds() >= HANG_WARN_SECS && !job->hungFlagged)
    {
      job->hungFlagged = true;
      if (job->dialog)
        job->dialog->markHung(true);
    }
  }

  if (m_jobs.empty())
    m_pollTimer.Stop();
}

void ExportJobManager::drainOutput(ExportJob * job)
{
  if (!job->process)
    return;

  // stdout: progress protocol + general output.
  if (job->process->IsInputAvailable())
  {
    wxInputStream * in = job->process->GetInputStream();
    char buf[4096];
    while (job->process->IsInputAvailable())
    {
      in->Read(buf, sizeof(buf));
      size_t n = in->LastRead();
      if (n == 0)
        break;
      wxString chunk = wxString::FromUTF8(buf, n);
      if (job->logFile.IsOpened())
        job->logFile.Write(chunk);

      job->lineBuf += chunk;
      int nl;
      while ((nl = job->lineBuf.Find('\n')) != wxNOT_FOUND)
      {
        wxString line = job->lineBuf.Left(nl);
        line.Replace(wxT("\r"), wxT(""));
        job->lineBuf = job->lineBuf.Mid(nl + 1);
        handleLine(job, line);
      }
    }
  }

  // stderr: log only.
  if (job->process->IsErrorAvailable())
  {
    wxInputStream * err = job->process->GetErrorStream();
    char buf[4096];
    while (job->process->IsErrorAvailable())
    {
      err->Read(buf, sizeof(buf));
      size_t n = err->LastRead();
      if (n == 0)
        break;
      if (job->logFile.IsOpened())
        job->logFile.Write(wxString::FromUTF8(buf, n));
    }
  }

  if (job->logFile.IsOpened())
    job->logFile.Flush();
}

void ExportJobManager::handleLine(ExportJob * job, const wxString & rawLine)
{
  const wxString PREFIX = wxT("WMVEXPORT-PROGRESS:");
  int idx = rawLine.Find(PREFIX);
  if (idx == wxNOT_FOUND)
    return; // ordinary log line; already written to the log file

  wxString payload = rawLine.Mid(idx + PREFIX.length());
  payload.Trim(true).Trim(false);
  if (payload.IsEmpty())
    return;

  job->lastProgress = wxDateTime::Now();
  if (job->hungFlagged && job->dialog)
  {
    job->hungFlagged = false; // progress resumed
  }

  wxString token = payload.BeforeFirst(' ');
  wxString rest  = payload.AfterFirst(' ');

  if (token == wxT("STAGE"))
  {
    job->stage = rest.Trim(true).Trim(false);
    if (job->dialog)
      job->dialog->setStage(job->stage);
    int p = stagePercent(job->stage);
    if (p >= 0 && p > job->percent)
      job->percent = p;
  }
  else if (token == wxT("TOTAL_CLIPS"))
  {
    long n = 0;
    rest.Trim(true).Trim(false).ToLong(&n);
    job->totalClips = (int)n;
  }
  else if (token == wxT("CLIP"))
  {
    // CLIP <current> <total> <name...>
    wxString s = rest;
    long cur = 0, tot = 0;
    wxString t1 = s.BeforeFirst(' '); s = s.AfterFirst(' ');
    wxString t2 = s.BeforeFirst(' '); s = s.AfterFirst(' ');
    t1.ToLong(&cur);
    t2.ToLong(&tot);
    job->clipNum   = (int)cur;
    if (tot > 0) job->totalClips = (int)tot;
    wxString name  = s;
    if (job->dialog)
      job->dialog->setClip(job->clipNum, job->totalClips, name);

    // Clips fill the 62..92 band proportionally.
    int p = 62;
    if (job->totalClips > 0)
      p = 62 + (30 * job->clipNum) / job->totalClips;
    if (p > 92) p = 92;
    if (p > job->percent) job->percent = p;
  }

  if (job->dialog)
    job->dialog->setPercent(job->percent);
}

void ExportJobManager::killChild(ExportJob * job)
{
  if (job->pid > 0)
    wxProcess::Kill(job->pid, wxSIGKILL, wxKILL_CHILDREN);
}

void ExportJobManager::onProcessTerminated(ExportJob * job, int exitCode)
{
  job->terminated = true;
  job->exitCode = exitCode;
  // Drain whatever the child wrote right before exiting.
  drainOutput(job);
  if (!job->lineBuf.IsEmpty())
  {
    handleLine(job, job->lineBuf);
    job->lineBuf.Clear();
  }
  // The wxProcess deletes itself after OnTerminate returns; drop our pointer first.
  job->process = NULL;
  finalize(job);
}

void ExportJobManager::finalize(ExportJob * job)
{
  // Authoritative result = the status sidecar the child wrote (the exit code can be
  // non-zero from the GUI-app teardown even on success, so we trust the sidecar).
  bool ok = false;
  wxString errMsg;
  if (wxFileName::FileExists(job->statusPath))
  {
    wxFFile sf;
    if (sf.Open(job->statusPath, wxT("r")))
    {
      wxString content;
      sf.ReadAll(&content);
      sf.Close();
      content.Trim(true).Trim(false);
      if (content.StartsWith(wxT("OK")))
        ok = true;
      else
      {
        // "ERROR\t<message>" or just an error blob.
        errMsg = content.AfterFirst('\t');
        if (errMsg.IsEmpty()) errMsg = content;
      }
    }
  }

  if (job->logFile.IsOpened())
  {
    job->logFile.Write(wxString::Format(wxT("\n----------------------------------------\n[manager] child exited (code %d); status=%s\n"),
                                        job->exitCode, ok ? wxT("OK") : wxT("FAIL")));
    job->logFile.Close();
  }

  // Tear down the dialog.
  if (job->dialog)
  {
    job->dialog->setPercent(ok ? 100 : job->percent);
    job->dialog->Destroy();
    job->dialog = NULL;
  }

  // Clean up sidecars/temp files we own.
  if (wxFileName::FileExists(job->lockPath))
    wxRemoveFile(job->lockPath);
  if (!job->tempCharPath.IsEmpty() && wxFileName::FileExists(job->tempCharPath))
    wxRemoveFile(job->tempCharPath);

  // Notify the user.
  if (job->cancelled)
  {
    LOG_INFO << "[export] cancelled:" << (const char*)job->outPath.mb_str();
    // No modal popup on a user-initiated cancel; the dialog already showed "cancelling".
    if (m_owner)
      m_owner->SetStatusText(wxT("Export cancelled."));
  }
  else if (ok)
  {
    LOG_INFO << "[export] SUCCESS:" << (const char*)job->outPath.mb_str();
    wxMessageBox(wxT("Export completed successfully:\n") + job->outPath,
                 wxT("Export done"), wxOK | wxICON_INFORMATION, m_owner);
  }
  else
  {
    LOG_ERROR << "[export] FAILED:" << (const char*)job->outPath.mb_str() << "reason:" << (const char*)errMsg.mb_str();
    wxString msg = wxT("The export did not complete.\n\n");
    if (!errMsg.IsEmpty())
      msg << errMsg << wxT("\n\n");
    else
      msg << wxT("The exporter stopped unexpectedly (it may have crashed or been stopped).\n\n");
    msg << wxT("A detailed log was written to:\n") << job->logPath;
    wxMessageBox(msg, wxT("Export Error"), wxOK | wxICON_ERROR, m_owner);
  }

  // Remove from the active list and free.
  m_jobs.remove(job);
  delete job;

  if (m_jobs.empty())
    m_pollTimer.Stop();
}
