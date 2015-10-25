#ifndef ITEMSELECTION_H
#define ITEMSELECTION_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

// wx
#include <wx/choicdlg.h>
class wxListEvent;
class wxListView;

// stl
#include <map>
#include <vector>

wxColour ItemQualityColour(int quality);

class CharControl;

class ChoiceDialog : public wxSingleChoiceDialog {
	int type;

    DECLARE_EVENT_TABLE()

public:
	CharControl *cc;
	wxListView *m_listctrl;
	ChoiceDialog(CharControl *dest, int type,
	                       wxWindow *parent,
                           const wxString& message,
                           const wxString& caption,
                           const wxArrayString& choices);

	virtual void OnClick(wxCommandEvent &event);
	void OnSelect(wxListEvent &event);
  virtual int GetSelection() const { return m_selection; }
	void EndModal(int retCode) { SetReturnCode(retCode); Hide(); }
	virtual void DoFilter() { };
	virtual void Check(int index, bool state) { };

};



class FilteredChoiceDialog: public ChoiceDialog {
protected:    
    wxTextCtrl* m_pattern;
    const wxArrayString* m_choices;
    std::vector<int> m_indices; // filtered index -> orig inndex
    
    DECLARE_EVENT_TABLE()

public:
    enum{
        ID_FILTER_TEXT = 1000,
        ID_FILTER_BUTTON,
        ID_FILTER_CLEAR,
        ID_IMPORT_NPC_BUTTON,
        ID_IMPORT_ITEM_BUTTON
    };

	bool keepFirst;
    
	FilteredChoiceDialog(CharControl *dest, int type,
	    wxWindow *parent,
	    const wxString& message,
	    const wxString& caption,
	    const wxArrayString& choices,
	    const std::vector<int> *quality,
	    bool keepfirst = true);

  virtual void OnFilter(wxCommandEvent& event);
  virtual void OnImportNPC(wxCommandEvent& event);
  virtual void OnImportItem(wxCommandEvent& event);
  virtual int GetSelection() const { return m_indices[m_selection]; }
  virtual bool FilterFunc(int index);
  virtual void DoFilter();
};


class CategoryChoiceDialog: public FilteredChoiceDialog {
protected:
	const std::vector<int> &m_cats;
	std::map<int,unsigned int> m_catsConvert;

	wxCheckListBox *m_catlist;
	int numcats;

    DECLARE_EVENT_TABLE()
public:
	enum {
		ID_CAT_LIST = 1100
	};

    CategoryChoiceDialog(CharControl *dest, int type,
	                       wxWindow *parent,
                           const wxString& message,
                           const wxString& caption,
                           const wxArrayString& choices,
						   const std::vector<int> &cats,
						   const wxArrayString& catnames,
						   const std::vector<int> *quality,
						   bool keepfirst = true,
						   bool helpmsg = true);

	virtual void Check(int index, bool state = true);
	virtual void OnCheck(wxCommandEvent &e);
	virtual void OnCheckDoubleClick(wxCommandEvent &e);
	virtual bool FilterFunc(int index);
};

#endif

