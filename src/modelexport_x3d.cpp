/*
 * WoWModelViewer X3D exporter
 * Author: Tobias Alexander Franke
 * Date: 1/12/2010
 *
 */

#include <wx/wfstream.h>
#include <math.h>

#include "modelexport.h"
#include "modelcanvas.h"

//#include "CxImage/ximage.h"

//#define NONSTANDARDBLENDMODE

class tabbed_ostream
{
private:
    size_t tabc_;
    std::string tab_;
    std::ostream& stream_;
    bool on_;

    void mtab() 
    {
        tab_ = "";
        for (size_t x = 0; x < tabc_; ++x)
            tab_ += "\t";
    };

public:
    tabbed_ostream(std::ostream& stream) : tabc_(0), tab_(""), stream_(stream), on_(true) 
    { 
        stream_.setf(std::ios::fixed);
        stream_.precision(6);
    }

    void toggle() { on_ = !on_; }
    void tab() { tabc_++; mtab(); }
    void rtab() { tabc_--; mtab(); }

    template<typename T>
    std::ostream& operator << (T c)
    {
        if (on_)
            stream_ << tab_.c_str();
        stream_ << c;
        return stream_;
    }
};

void writeBlendMode(tabbed_ostream& s, int16 blendmode)
{
    if (blendmode == BM_OPAQUE)
        return;
    
    s   << "<BlendMode ";

    s.toggle();
    switch(blendmode)
    {
        case BM_TRANSPARENT:
            s << "alphaFunc='equal' alphaFuncValue='0.7'";
            break;
        case BM_ALPHA_BLEND:
            s << "srcFactor='src_alpha' destFactor='one_minus_src_alpha'";
            break;
        case BM_ADDITIVE:
            break;
        case BM_ADDITIVE_ALPHA:
            break;
        case BM_MODULATE:
            break;
        case BM_MODULATEX2:
            break;
        default:
            break;
    }
    
    s << " />" << std::endl;
    s.toggle();    
}

Vec3D calcCenteringTransform(Model* m, bool init)
{
    Vec3D boundMax, boundMin;

    float* bmaxf = boundMax;
    float* bminf = boundMin;

    if (m->vertices == NULL || init == true) 
        boundMax = boundMin = m->origVertices[0].pos;
    else
        boundMax = boundMin = m->vertices[0];

    for (size_t x = 0; x < m->header.nVertices; ++x)
    {
        float* bf;
        
        if (m->vertices == NULL || init == true)
            bf = static_cast<float*>(m->origVertices[x].pos);
        else
            bf = static_cast<float*>(m->vertices[x]);
        
        for (size_t i = 0; i < 3; ++i)
        {
            if (bf[i] > bmaxf[i])
                bmaxf[i] = bf[i];

            if (bf[i] < bminf[i])
                bminf[i] = bf[i];
        }
    }

    return (boundMin+((boundMax-boundMin)*0.5f))*-1.f;
}

// find the biggest "times" keyframe array to determine the maximum number of keyframes that can be saved
size_t getMaxKeyFrames(Model* m)
{
    size_t anim = m->animManager->GetAnim();
    size_t numKeyFrames = 0;

    // figure out a reasonable number of keyframes
    for (size_t b = 0; b < m->header.nBones; ++b)
    {
        size_t nTrans = m->bones[b].trans.times[anim].size();
        size_t nScale = m->bones[b].scale.times[anim].size();
        size_t nRotat = m->bones[b].rot.times[anim].size();


        if (nTrans) numKeyFrames = max(nTrans, numKeyFrames);
        if (nScale) numKeyFrames = max(nScale, numKeyFrames);
        if (nRotat) numKeyFrames = max(nRotat, numKeyFrames);
    }

    return numKeyFrames;
}

// ask user for number of keyframes per second to save
size_t getKFPSExportOptions(Model* m)
{
    // default value
    size_t maxKeyFrames = getMaxKeyFrames(m);
    wxString strNumFrames(wxT("5"));
    wxString title = wxString::Format(wxT("Number of key frames per second to be used (max. %i):"), maxKeyFrames);

    wxTextEntryDialog d(NULL, title, wxT("Export X3D animation"), strNumFrames);

    double n;
    bool success;
    size_t numFrames;

    do
    {
        do
        {
            if (d.ShowModal() == wxID_OK)
                strNumFrames = d.GetValue();
            else
                return maxKeyFrames;
        }
        while (!strNumFrames.IsNumber());

        success = strNumFrames.ToDouble(&n);
        numFrames = static_cast<size_t>(n);
    } 
    while (!success || numFrames > maxKeyFrames || n < 1);

    return numFrames;
}

void M2toX3DAnim(tabbed_ostream s, Model* m)
{
    if (m->header.nBones == 0)
        return;

    // get total frame count of model
    size_t numTotalFrames = m->animManager->GetFrameCount();

    // determine number of keyframes per second to be used
    size_t numKFPS = getKFPSExportOptions(m);

    // animManager->getFrameCount total frames equal milliseconds, because
    // Tick() progresses the Frame counter by time*speed, time being time passed in milliseconds
    float totalTime = static_cast<float>(numTotalFrames)/1000.f;

    s   << "<TimeSensor DEF='ts' "
        << " cycleInterval='" << totalTime <<  "' "
        << " loop='TRUE' />" << std::endl;

    // length of animation * key frames per second = numKeyFrames
    size_t numKeyFrames = static_cast<size_t>(totalTime * numKFPS);
    assert(numKeyFrames < numTotalFrames);

    // length of step per time unit
    size_t stepSize = numTotalFrames/numKeyFrames;
    
    // save state
    bool showModel = m->showModel;
    m->showModel = false;
    size_t currentFrame = m->animManager->GetFrame();

    for (size_t i=0; i<m->passes.size(); i++) 
    {
        ModelRenderPass &p = m->passes[i];

        if (p.init(m)) 
        {
            s << "<CoordinateInterpolator DEF='ci_" << i << "' ";
            s.toggle();
            s << "key='";

            for (size_t frame = 0; frame < numKeyFrames; ++frame)
                s << static_cast<float>(frame)/static_cast<float>(numKeyFrames) << " ";
            s << " 1.0' keyValue='";

            for (size_t frame = 0; frame <= numKeyFrames; ++frame)
            {
                m->animManager->SetFrame(((stepSize*frame) % numTotalFrames));

                // calculate frame
                m->draw();

                //s << "<!-- frame " << frame << " -->" << std::endl;

                for (size_t k = 0, b=p.indexStart; k<p.indexCount; k++,b++)
                {
                    uint16 a = m->indices[b];
                    Vec3D v;

                    v = m->vertices[a];

                    s << v.x << " " << v.y << " " << v.z << " " << std::endl;
                }
            }
            s << "' />" << std::endl;
            s.toggle();

            s << "<ROUTE fromNode='ts' fromField='fraction_changed' toNode='ci_" << i << "' toField='set_fraction' />" << std::endl;
            s << "<ROUTE fromNode='ci_" << i << "' fromField='value_changed' toNode='pl_" << i << "' toField='set_point' />" << std::endl;
        }
    }

    // reset state
    m->showModel = showModel;
    m->animManager->SetFrame(currentFrame);
}

void M2toX3D(tabbed_ostream s, Model *m, bool init, const char* fn, bool xhtml)
{
	LogExportData(wxT("X3D"),m->modelname,wxString(fn, wxConvUTF8));
    s << "<!-- Exported with WoWModelViewer -->" << std::endl;

    s.tab();
    s << "<Scene>" << std::endl;
    
    s.tab();

    // define background
    s   << "<Background skyColor='" 
        << 70.f/256.f << " " 
        << 94.f/256.f << " "
        << 121.f/256.f << " "
        << "'/>" << std::endl;

    // define all textures
    typedef std::map<int, wxString> texMap;
    texMap textures;

    if (!fn)
        fn = "texture";

    size_t num_rot = 0;
    if (modelExport_X3D_CenterModel)
    {
        // move viewpoint back
        s << "<Viewpoint position='0 0 " << m->pos.z << "'/>" << std::endl;

        // create rotation transforms only if there is an actual rotation
        float rot[3] = { m->rot.x, m->rot.y, m->rot.z };
        for (size_t n=0; n < 3; ++n)
            if (rot[n] != 0.f)
            {   
                s << "<Transform rotation='" << (n == 0) << " " << (n == 1) << " " << (n == 2) << " " 
                  << PIOVER180d*rot[n] << "'>" << std::endl;
                num_rot++;
                s.tab();
            }

        // translate object to center
        Vec3D p = calcCenteringTransform(m, init);
        s << "<Transform translation='" << p.x << " " << p.y << " " << p.z << "'>" << std::endl;
        s.tab();
    }

    for (size_t i=0; i<m->passes.size(); i++) 
    {
        ModelRenderPass &p = m->passes[i];
        if (p.init(m)) 
        {
            s << "<Shape>"  << std::endl;

            size_t counter;

            // write material, color etc.

            s.tab();
            s << "<Appearance>" << std::endl;

            s.tab();

#ifdef NONSTANDARDBLENDMODE
            if (!xhtml)
            {
                writeBlendMode(s, p.blendmode);
                if (p.noZWrite)
                    s << "<DepthMode readOnly='TRUE' />" << std::endl;
            }
#endif
            
            s   << "<Material diffuseColor='"
                << p.ocol.x << " " 
                << p.ocol.y << " " 
                << p.ocol.z << "' ";

            s.toggle();

            if (p.useEnvMap)
                s << "shininess='18.0' ";

#ifndef NONSTANDARDBLENDMODE
            if (!xhtml) 
            {
                s   << "emissiveColor='" 
                    << p.ecol.x << " " 
                    << p.ecol.y << " " 
                    << p.ecol.z << "' "

                    << "transparency='" << 1.f - p.ocol.w << "' ";
            }
#endif
            s << "/>" << std::endl;
            s.toggle();

            // has the texture been used before?
            // no : save texture, create a map entry and define it
            // yes: reuse the previously defined texture
            if (!textures.count(p.tex))
            {
                wxString texName(fn, wxConvUTF8);
                texName = texName.AfterLast(SLASH).BeforeLast('.');
                texName << wxT("_") << p.tex;

                wxString texFilename(fn, wxConvUTF8);
                texFilename = texFilename.BeforeLast(SLASH);
                texFilename += SLASH;
                texFilename += texName;
                texFilename += wxString(wxT(".png"));
                SaveTexture(texFilename);

                textures[p.tex] = texName;

                s << "<ImageTexture DEF='" << textures[p.tex] << "' url='"
                  << texFilename.AfterLast(SLASH).c_str() << "' />" << std::endl;
            }
            else
                s << "<ImageTexture USE='" << textures[p.tex] << "'/>" << std::endl;

            s.rtab();
            s << "</Appearance>" << std::endl;

            // ---------------- write all indices here -----------------

            s << "<IndexedFaceSet solid='false' ";
            s.toggle();

            // write normals
            counter = 0;
            s << "coordIndex='" << std::endl;
            for (size_t k=0; k<p.indexCount; k+=3)
            {
                s << counter+1 << " " << counter+2 << " " << counter+0 << " -1 ";
                counter += 3;
            }
            s << "'>" << std::endl;
            s.toggle();

            // ----------------- write all data here -----------------

            // write vertices
            s.tab();
            s << "<Coordinate DEF='pl_" << i << "' point='" << std::endl;
            s.tab();
            for (size_t k = 0, b=p.indexStart; k<p.indexCount; k++,b++) 
            {
                uint16 a = m->indices[b];
                Vec3D v;

                if (m->vertices == NULL || init == true) 
                    v = m->origVertices[a].pos;
                else
                    v = m->vertices[a];

                s << v.x << " " << v.y << " " << v.z << std::endl;
            }
            s.rtab();
            s << "' />" << std::endl;

            // write normals
            s << "<Normal vector='" << std::endl;
            s.tab();
            for (size_t k = 0, b=p.indexStart; k<p.indexCount; k++,b++) 
            {
                uint16 a = m->indices[b];
                Vec3D v = m->origVertices[a].normal;

                s << v.x << " " << v.y << " " << v.z << std::endl;
            }
            s.rtab();
            s << "' />" << std::endl;

            // write texcoords
            s << "<TextureCoordinate point='" << std::endl;
            s.tab();
            for (size_t k = 0, b=p.indexStart; k<p.indexCount; k++,b++)
            {
                uint16 a = m->indices[b];
                Vec2D v = m->origVertices[a].texcoords;
                s << v.x << " " << 1-v.y << std::endl;
            }
            s.rtab();
            s << "' />" << std::endl;

            s.rtab();
            s << "</IndexedFaceSet>" << std::endl;

            s.rtab();
            s << "</Shape>" << std::endl << std::endl;
        }
    }

    if (m->animated && modelExport_X3D_ExportAnimation)
        M2toX3DAnim(s, m);

    if (modelExport_X3D_CenterModel)
    {
        // close translation
        s.rtab();
        s << "</Transform>" << std::endl;

        // close all rotations
        for (size_t n = 0; n < num_rot; ++n)
        {
            s.rtab();
            s << "</Transform>" << std::endl;
        }
    }

    s.rtab();
    s << "</Scene>" << std::endl;

    s.rtab();
}

void ExportX3D_M2(Model *m, const char *fn, bool init)
{
	// FIXME: ofstream is not compitable with multibyte path name
    ofstream f(fn, ios_base::out | ios_base::trunc);

    if (f.good())
    {
        f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl;
        f << "<!DOCTYPE X3D PUBLIC \"ISO//Web3D//DTD X3D 3.0//EN\" \"http://www.web3d.org/specifications/x3d-3.0.dtd\">" << std::endl;
        f << "<X3D xmlns:xsd='http://www.w3.org/2001/XMLSchema-instance' profile='Full' version='3.0' xsd:noNamespaceSchemaLocation='http://www.web3d.org/specifications/x3d-3.0.xsd'>" << std::endl;
        M2toX3D(f, m, init, fn, false);
        f << "</X3D>" << std::endl;
    }

    f.close();
}

void ExportXHTML_M2(Model *m, const char *fn, bool init)
{
	// FIXME: ofstream is not compitable with multibyte path name
    ofstream f(fn, ios_base::out | ios_base::trunc);

    if (f.good())
    {
        // write xhtml stuff for WebGL
        f << "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">" << std::endl;
        f << "<html xmlns=\"http://www.w3.org/1999/xhtml\">" << std::endl;
        f << "<head>" << std::endl;
        f << "    <meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\" />" << std::endl;
        f << "    <title>WoWModelViewer X3D export</title>" << std::endl;
        f << "    <link rel=\"stylesheet\" type=\"text/css\" href=\"http://www.x3dom.org/x3dom/src/x3dom.css\" />" << std::endl;
        f << "</head>" << std::endl;
        f << "<body>" << std::endl;
        f << "    <p> " << std::endl;
        f << "    <X3D xmlns=\"http://www.web3d.org/specifications/x3d-namespace\" id=\"someUniqueId\" showStat=\"false\" showLog=\"false\" x=\"0px\" y=\"0px\" width=\"600px\" height=\"600px\">" << std::endl;

        M2toX3D(f, m, init, fn, true);

        f << "\n" << std::endl;
        f << "      </X3D>" << std::endl;
        f << "    </p>" << std::endl;
        f << "    <script type=\"text/javascript\" src=\"http://www.x3dom.org/x3dom/src/x3dom.js\"></script>" << std::endl;
        f << "</body>" << std::endl;
        f << "</html>" << std::endl;
    }

    f.close();
}
