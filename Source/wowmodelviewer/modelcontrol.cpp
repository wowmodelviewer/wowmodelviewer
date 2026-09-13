#include "modelcontrol.h"

#include "ximage.h"

#include <wx/wx.h>
#include <wx/statline.h>
#include <wx/ffile.h>
#include <wx/textctrl.h>

#include "Attachment.h"
#include "enums.h"
#include "globalvars.h"
#include "ModelInspector.h"
#include "modelviewer.h"
#include "WoWItem.h"

#include "logger/Logger.h"



IMPLEMENT_CLASS(ModelControl, wxWindow)

BEGIN_EVENT_TABLE(ModelControl, wxWindow)
  EVT_COMBOBOX(ID_MODEL_NAME, ModelControl::OnCombo)
  EVT_COMBOBOX(ID_MODEL_LOD, ModelControl::OnCombo)

  EVT_CHECKBOX(ID_MODEL_BONES, ModelControl::OnCheck)
  EVT_CHECKBOX(ID_MODEL_BOUNDS, ModelControl::OnCheck)
  EVT_CHECKBOX(ID_MODEL_RENDER, ModelControl::OnCheck)
  EVT_CHECKBOX(ID_MODEL_WIREFRAME, ModelControl::OnCheck)
  EVT_CHECKBOX(ID_MODEL_PARTICLES, ModelControl::OnCheck)
  EVT_CHECKBOX(ID_MODEL_TEXTURE, ModelControl::OnCheck)

  EVT_COMMAND_SCROLL(ID_MODEL_ALPHA, ModelControl::OnSlider)
  EVT_COMMAND_SCROLL(ID_MODEL_SCALE, ModelControl::OnSlider)
  EVT_TEXT_ENTER(ID_MODEL_SIZE, ModelControl::OnEnter)

END_EVENT_TABLE()


// ModelName
// LevelOfDetail
// Opacity
// Bones
// Bounding Box
// Render
// Geosets
// Future Additions:
//    - Pos
//    - Rotation
//    - Scale
//    - Attach model

ModelControl::ModelControl(wxWindow* parent, wxWindowID id)
 : wxWindow(parent, id, wxDefaultPosition, wxSize(120, 550), 0,  wxT("ModelControlFrame"))
{
  model = NULL;
  att = NULL;

  LOG_INFO << "Creating Model Control...";

  wxFlexGridSizer *padding = new wxFlexGridSizer(1,1,0);

  wxFlexGridSizer *top = new wxFlexGridSizer(1,5,0);
  modelname = new wxComboBox(this, ID_MODEL_NAME);
  top->Add(modelname, 1, wxEXPAND);

/*
  cbLod = new wxComboBox(this, ID_MODEL_LOD);
  top->AddSpacer(5);
  top->Add(new wxStaticText(this, wxID_ANY, wxT("View")), 1, wxEXPAND);
  top->Add(cbLod, 1, wxEXPAND);
*/

  top->AddSpacer(5);
  alpha = new wxSlider(this, ID_MODEL_ALPHA, 100, 0, 100);
  top->Add(new wxStaticText(this, wxID_ANY, wxT("Alpha")), 1, wxEXPAND);
  top->Add(alpha, 1, wxEXPAND);

  wxFlexGridSizer * gbox = new wxFlexGridSizer(2, 5, 5);
  gbox->Add(new wxStaticText(this, wxID_ANY, wxT("Scale")), 1, wxALIGN_CENTER_VERTICAL);
  txtsize = new wxTextCtrl(this, ID_MODEL_SIZE, wxT("1.00"));
  gbox->Add(txtsize);
  top->Add(gbox, 1, wxEXPAND);
  scale = new wxSlider(this, ID_MODEL_SCALE, 100, 10, 300);
  top->Add(scale, 1, wxEXPAND);

  top->AddSpacer(5);
  gbox = new wxFlexGridSizer(2, 5, 5);
  bones = new wxCheckBox(this, ID_MODEL_BONES, wxT("Bones"));
  wireframe = new wxCheckBox(this, ID_MODEL_WIREFRAME, wxT("Wireframe"));
  gbox->Add(bones);
  gbox->Add(wireframe);
  box = new wxCheckBox(this, ID_MODEL_BOUNDS, wxT("Bounds"));
  texture = new wxCheckBox(this, ID_MODEL_TEXTURE, wxT("Texture"));
  gbox->Add(box);
  gbox->Add(texture);
  render = new wxCheckBox(this, ID_MODEL_RENDER, wxT("Render"));
  particles = new wxCheckBox(this, ID_MODEL_PARTICLES, wxT("Particles"));
  gbox->Add(render);
  gbox->Add(particles);
  top->Add(gbox, 1, wxEXPAND);

  // The geoset list that used to follow here is the Model panel's Geosets tab now.
  top->AddGrowableCol(0);
  top->SetSizeHints(this);
  Show(true);
  SetAutoLayout(true);
  padding->Add(top, 1, wxEXPAND|wxLEFT|wxTOP, 10);
  padding->AddGrowableCol(0);
  SetSizer(padding);
  Layout();
}

ModelControl::~ModelControl()
{
  modelname->Destroy();
  // cbLod->Destroy();
  alpha->Destroy();
  scale->Destroy();
  bones->Destroy();
  box->Destroy();
  render->Destroy();
  wireframe->Destroy();
  texture->Destroy();
  particles->Destroy();
}

// Iterates through all the models counting and creating a list
void ModelControl::RefreshModel(Attachment *root)
{
  try {
    attachments.clear();

    WoWModel *m = static_cast<WoWModel*>(root->model());
    if (m) {
    //  wxASSERT(m);
      attachments.push_back(root);
      if (!init)
        UpdateModel(root);
      LOG_INFO << "ModelControl Refresh: Adding Model...";
    }
    
    for (std::vector<Attachment *>::iterator it=root->children.begin(); it!=root->children.end(); ++it) {
      //m = NULL;
      m = static_cast<WoWModel*>((*it)->model());
      if (m) {
        attachments.push_back((*it));
        if (!init)
          UpdateModel((*it));
        LOG_INFO << "ModelControl Refresh: Adding Attachment" << m->name() << "at level 1...";
      }

      for (std::vector<Attachment *>::iterator it2=(*it)->children.begin(); it2!=(*it)->children.end(); ++it2) {
        m = static_cast<WoWModel*>((*it2)->model());
        if (m) {
          //models.push_back(m);
          attachments.push_back((*it2));
          if (!init)
            UpdateModel((*it2));
          LOG_INFO << "ModelControl Refresh: Adding Attachment" << m->name() << "at level 2...";
        }

        for (std::vector<Attachment *>::iterator it3=(*it2)->children.begin(); it3!=(*it2)->children.end(); ++it3) {
          m = static_cast<WoWModel*>((*it3)->model());
          if (m) {
            //models.push_back(m);
            attachments.push_back((*it3));
            if (!init)
              UpdateModel((*it3));
            LOG_INFO << "ModelControl Refresh: Adding Attachment" << m->name() << "at level 3...";
          }
        }
      }
    }

    // update combo box with the list of models?
    wxString tmp;
    modelname->Clear();
    for (std::vector<Attachment*>::iterator it=attachments.begin(); it!=attachments.end(); ++it) {
      m = dynamic_cast<WoWModel*>((*it)->model());
      if (m) {
        tmp = m->name().toStdWString();
        modelname->Append(tmp.AfterLast('\\'));
      }
    }

    LOG_INFO << "ModelControl Refresh: Found" << attachments.size() << "Models...";

    if (modelname->GetCount() > 0)
      modelname->SetSelection(0);

  } catch( ... ) {
    LOG_ERROR << "Problem occured in ModelControl::RefreshModel(Attachment *)";
  }

}

void ModelControl::UpdateModel(Attachment *a)
{
  if (!a)
    return;

  init = false;

  WoWModel *m = NULL;
  if (a->model())
    m = static_cast<WoWModel*>(a->model());

  if (m) {
    init = true;
    model = m;
    att = a;

    modelname->SetLabel(m->name().toStdWString());

    Update();
  }
}

void ModelControl::Update()
{
  if (!model)
    return;

/*
  // Set view code is disabled / doesn't work, so I just removed this widget for now - Wayne
  // Loop through all the views.
  cbLod->Clear();
  
  int numViews = sizeof(model->skinFileIDs);
  
  if (numViews == 1)
  {
    cbLod->Append(wxT("1 (Only View)"));
  }
  else
  {
    cbLod->Append(wxT("1 (Worst)")); //Pretty sure lowest is actually BEST view - Wayne
    for (size_t i=0; i<numViews; i++)
    { 
      cbLod->Append(wxString::Format(wxT("%i%s"), i+1, (i==numViews-1) ? " (Best)" : ""));
    }
  }
  cbLod->SetSelection(0);
*/

  bones->SetValue(model->showBones);
  box->SetValue(model->showBounds);
  render->SetValue(model->showModel);
  wireframe->SetValue(model->showWireframe);
  particles->SetValue(model->showParticles);
  texture->SetValue(model->showTexture);

  alpha->SetValue(int(model->alpha_ * 100));
  scale->SetValue(model->scale_*100);

  txtsize->SetValue(wxString::Format(wxT("%.2f"), model->scale_));

}

// The geoset checkboxes live in the Model panel's Geosets tab; callers that changed the display
// flags (a skin change) still come through here.
void ModelControl::UpdateGeosetSelection()
{
  if (g_modelViewer && g_modelViewer->modelInspector)
    g_modelViewer->modelInspector->UpdateGeosetSelection();
}

void ModelControl::OnCheck(wxCommandEvent &event)
{
  if (!init || !model)
    return;

  int id = event.GetId();
  bool check = event.IsChecked();
  switch (id)
  {
    case ID_MODEL_BONES :
          model->showBones = check;
          break;
    case ID_MODEL_BOUNDS :
          model->showBounds = check;
          break;
    case ID_MODEL_RENDER :
          model->showModel = check;
          {
            // A character hides the hair/ears/horns an equipped helm would cover. That auto-hide
            // only makes sense while the helm is drawn, so if we just toggled a head-item's model,
            // re-run the character refresh: the covered geosets reappear when the helm is hidden
            // and hide again when it is re-shown.
            WoWModel * charModel = attachments.empty()
                                 ? NULL : dynamic_cast<WoWModel*>(attachments[0]->model());
            if (charModel && charModel != model && charModel->isEquippedHeadModel(model))
              charModel->refresh();
          }
          break;
    case ID_MODEL_WIREFRAME :
          model->showWireframe = check;
          break;
    case ID_MODEL_PARTICLES :
          model->showParticles = check;
          break;
    case ID_MODEL_TEXTURE :
          model->showTexture = check;
          break;
  }
}

void ModelControl::OnCombo(wxCommandEvent &event)
{
  if (!init)
    return;

  int id = event.GetId();

  if (id == ID_MODEL_LOD) {
//    int value = event.GetInt();
//
//    MPQFile f(model->name);
//    if (f.isEof() || (f.getSize() < sizeof(ModelHeader))) {
//      LOG_ERROR << "Unable to open MPQFile:" << model->name.c_str();
//      f.close();
//      return;
//    }
//
//    model->showModel = false;
//    model->setLOD(&f, value);
//    model->showModel = true;
//
//    /*
//    for (size_t i=0; i<model->geosets.size(); i++) {
//      int id = model->geosets[i].id;
//      model->showGeosets[i] = (id==0);
//    }
//
//    cc->RefreshModel();
//    */
//
//    f.close();
  } else if (id == ID_MODEL_NAME) {
    /* Alfred 2009/07/16 fix crash, remember CurrentSelection before UpdateModel() */
    int CurrentSelection = modelname->GetCurrentSelection();
    if (CurrentSelection < (int)attachments.size()) {
      UpdateModel(attachments[CurrentSelection]);
      att = attachments[CurrentSelection];
      model = static_cast<WoWModel*>(attachments[CurrentSelection]->model());
      
      animControl->UpdateModel(model);
      modelname->SetSelection(CurrentSelection);
    }
  }
}

void ModelControl::OnSlider(wxScrollEvent &event)
{
  if (!init || !model)
    return;

  int id = event.GetId();
  if (id == ID_MODEL_ALPHA) {
    model->alpha_ = event.GetInt() / 100.0f;
  } else if (id == ID_MODEL_SCALE) {
    model->scale_ = event.GetInt() / 100.0f;
    txtsize->SetValue(wxString::Format(wxT("%.2f"), model->scale_));
  }
}


void ModelControl::OnEnter(wxCommandEvent &event)
{
  if (!init || !model)
    return;

  int eventID = event.GetId();


  if (eventID == ID_MODEL_SIZE)
  {
    model->scale_ = wxAtof(txtsize->GetValue());
    scale->SetValue(wxAtoi(txtsize->GetValue())*100);
  }
}





/**************************************************************************
  * ScrWindow
  *************************************************************************/

ScrWindow::ScrWindow(const wxString& title)
       : wxFrame(NULL, wxID_ANY, title, wxDefaultPosition, wxSize(512, 512))
{
  wxImage::AddHandler(new wxPNGHandler);
  sw = new wxScrolledWindow(this);

  wxBitmap bmp(title, wxBITMAP_TYPE_PNG);
  sb = new wxStaticBitmap(sw, -1, bmp);

  int width = bmp.GetWidth();
  int height = bmp.GetHeight();

  CreateStatusBar();
  wxString sbarText;
  sbarText.Printf(wxT("%ix%i"), width, height);
  SetStatusText(sbarText);

  sw->SetScrollbars(10, 10, width/10, height/10);
//  sw->Scroll(50,10);

  Center();
}

ScrWindow::~ScrWindow()
{
  sb->Destroy();
  sw->Destroy();
}
