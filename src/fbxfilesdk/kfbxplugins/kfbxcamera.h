/*!  \file kfbxcamera.h
 */

#ifndef FBXFILESDK_KFBXPLUGINS_KFBXCAMERA_H
#define FBXFILESDK_KFBXPLUGINS_KFBXCAMERA_H

/**************************************************************************************

 Copyright (C) 2001 - 2010 Autodesk, Inc. and/or its licensors.
 All Rights Reserved.

 The coded instructions, statements, computer programs, and/or related material 
 (collectively the "Data") in these files contain unpublished information 
 proprietary to Autodesk, Inc. and/or its licensors, which is protected by 
 Canada and United States of America federal copyright law and by international 
 treaties. 
 
 The Data may not be disclosed or distributed to third parties, in whole or in
 part, without the prior written consent of Autodesk, Inc. ("Autodesk").

 THE DATA IS PROVIDED "AS IS" AND WITHOUT WARRANTY.
 ALL WARRANTIES ARE EXPRESSLY EXCLUDED AND DISCLAIMED. AUTODESK MAKES NO
 WARRANTY OF ANY KIND WITH RESPECT TO THE DATA, EXPRESS, IMPLIED OR ARISING
 BY CUSTOM OR TRADE USAGE, AND DISCLAIMS ANY IMPLIED WARRANTIES OF TITLE, 
 NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE OR USE. 
 WITHOUT LIMITING THE FOREGOING, AUTODESK DOES NOT WARRANT THAT THE OPERATION
 OF THE DATA WILL BE UNINTERRUPTED OR ERROR FREE. 
 
 IN NO EVENT SHALL AUTODESK, ITS AFFILIATES, PARENT COMPANIES, LICENSORS
 OR SUPPLIERS ("AUTODESK GROUP") BE LIABLE FOR ANY LOSSES, DAMAGES OR EXPENSES
 OF ANY KIND (INCLUDING WITHOUT LIMITATION PUNITIVE OR MULTIPLE DAMAGES OR OTHER
 SPECIAL, DIRECT, INDIRECT, EXEMPLARY, INCIDENTAL, LOSS OF PROFITS, REVENUE
 OR DATA, COST OF COVER OR CONSEQUENTIAL LOSSES OR DAMAGES OF ANY KIND),
 HOWEVER CAUSED, AND REGARDLESS OF THE THEORY OF LIABILITY, WHETHER DERIVED
 FROM CONTRACT, TORT (INCLUDING, BUT NOT LIMITED TO, NEGLIGENCE), OR OTHERWISE,
 ARISING OUT OF OR RELATING TO THE DATA OR ITS USE OR ANY OTHER PERFORMANCE,
 WHETHER OR NOT AUTODESK HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH LOSS
 OR DAMAGE. 

**************************************************************************************/
#include <fbxfilesdk/fbxfilesdk_def.h>

#include <fbxfilesdk/kfbxplugins/kfbxnodeattribute.h>
#include <fbxfilesdk/kfbxplugins/kfbxcolor.h>

#include <fbxfilesdk/kfbxmath/kfbxvector4.h>

#include <fbxfilesdk/components/kbaselib/klib/kstring.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

class KFbxTexture;
class KFbxSdkManager;
class KFbxMatrix;
class KFbxXMatrix;
class KTime;

/** \brief This node attribute contains methods for accessing the properties of a camera.
  * \nosubgrouping
  * A camera can be set to automatically point at and follow
  * another node in the hierarchy. To do this, the focus source
  * must be set to ECameraFocusDistanceSource::eCAMERA_INTEREST and the
  * followed node associated with function KFbxNode::SetTarget().
  * \see KFbxCameraStereo KFbxCameraSwitcher
  */
class KFBX_DLL KFbxCamera : public KFbxNodeAttribute
{
    KFBXOBJECT_DECLARE(KFbxCamera,KFbxNodeAttribute);

public:
    //! Return the type of node attribute which is EAttributeType::eCAMERA.
    virtual EAttributeType GetAttributeType() const;

    //! Reset the camera to default values.
    void Reset();

    /** Camera projection types.
      * \enum ECameraProjectionType Camera projection types.
      * - \e ePERSPECTIVE
      * - \e eORTHOGONAL
      * \remarks     By default, the camera projection type is set to ePERSPECTIVE.
      *              If the camera projection type is set to eORTHOGONAL, the following options
      *              are not relevant:
      *                   - aperture format
      *                   - aperture mode
      *                   - aperture width and height
      *                   - angle of view/focal length
      *                   - squeeze ratio
      */
    typedef enum
    {
        ePERSPECTIVE,
        eORTHOGONAL
    } ECameraProjectionType;


	/**
      * \name Viewing Area Functions
      */
    //@{

        /** \enum ECameraFormat Camera formats.
          * - \e eCUSTOM_FORMAT
          * - \e eD1_NTSC
          * - \e eNTSC
          * - \e ePAL
          * - \e eD1_PAL
          * - \e eHD
          * - \e e640x480
          * - \e e320x200
          * - \e e320x240
          * - \e e128x128
          * - \e eFULL_SCREEN
          */
        typedef enum
        {
            eCUSTOM_FORMAT,
            eD1_NTSC,
            eNTSC,
            ePAL,
            eD1_PAL,
            eHD,
            e640x480,
            e320x200,
            e320x240,
            e128x128,
            eFULL_SCREEN
        } ECameraFormat;

        /** Set the camera format.
          * \param pFormat     The camera format identifier.
          * \remarks           Changing the camera format sets the camera aspect
          *                    ratio mode to eFIXED_RESOLUTION and modifies the aspect width
          *                    size, height size, and pixel ratio accordingly.
          */
        void SetFormat(ECameraFormat pFormat);

        /** Get the camera format.
          * \return     The current camera format identifier.
          */
        ECameraFormat GetFormat() const;

        /** \enum ECameraAspectRatioMode Camera aspect ratio modes.
          * - \e eWINDOW_SIZE
          * - \e eFIXED_RATIO
          * - \e eFIXED_RESOLUTION
          * - \e eFIXED_WIDTH
          * - \e eFIXED_HEIGHT
          */
        typedef enum
        {
            eWINDOW_SIZE,
            eFIXED_RATIO,
            eFIXED_RESOLUTION,
            eFIXED_WIDTH,
            eFIXED_HEIGHT
        } ECameraAspectRatioMode;

        /** Set the camera aspect.
          * \param pRatioMode     Camera aspect ratio mode.
          * \param pWidth         Camera aspect width, must be a positive value.
          * \param pHeight        Camera aspect height, must be a positive value.
          * \remarks              Changing the camera aspect sets the camera format to eCustom.
          *                            - If the ratio mode is eWINDOW_SIZE, both width and height values aren't relevant.
          *                            - If the ratio mode is eFIXED_RATIO, the height value is set to 1.0 and the width value is relative to the height value.
          *                            - If the ratio mode is eFIXED_RESOLUTION, both width and height values are in pixels.
          *                            - If the ratio mode is eFIXED_WIDTH, the width value is in pixels and the height value is relative to the width value.
          *                            - If the ratio mode is eFIXED_HEIGHT, the height value is in pixels and the width value is relative to the height value.
          */
        void SetAspect(ECameraAspectRatioMode pRatioMode, double pWidth, double pHeight);

        /** Get the camera aspect ratio mode.
          * \return     The current aspect ratio identifier.
          */
        ECameraAspectRatioMode GetAspectRatioMode() const;

        /** Set the pixel ratio.
          * \param pRatio     The pixel ratio value.
          * \remarks          The value must be a positive number. Comprised between 0.05 and 20.0. Values
          *                   outside these limits will be clamped. Changing the pixel ratio sets the camera format to eCUSTOM_FORMAT.
          */
        void SetPixelRatio(double pRatio);

        /** Get the pixel ratio.
          * \return     The current camera's pixel ratio value.
          */
        double GetPixelRatio() const;

        /** Set the near plane distance from the camera.
          * The near plane is the minimum distance to render a scene on the camera display.
	      * A synonym for the near plane is "front clipping plane".
          * \param pDistance     The near plane distance value.
          * \remarks             The near plane value is limited to the range [0.001, 600000.0] and
          *                      must be inferior to the far plane value.
          */
        void SetNearPlane(double pDistance);

        /** Get the near plane distance from the camera.
          * The near plane is the minimum distance to render a scene on the camera display.
	      * A synonym for the near plane is "front clipping plane".
          * \return     The near plane value.
          */
        double GetNearPlane() const;

        /** Set the far plane distance from camera.
          * The far plane is the maximum distance to render a scene on the camera display.
	      * A synonym for the far plane is "back clipping plane".
          * \param pDistance     The far plane distance value.
          * \remarks             The far plane value is limited to the range [0.001, 600000.0] and
          *                      must be superior to the near plane value.
          */
        void SetFarPlane(double pDistance);

        /** Get the far plane distance from camera.
          * The far plane is the maximum distance to render a scene on the camera display.
	      * A synonym for the far plane is "back clipping plane".
          * \return     The far plane value.
          */
        double GetFarPlane() const;

    //@}

    /**
      * \name Aperture and Film Functions
      * The aperture mode determines which values drive the camera aperture. When the aperture mode is \e eHORIZONTAL_AND_VERTICAL,
      * \e eHORIZONTAL or \e eVERTICAL, the field of view is used. When the aperture mode is \e eFOCAL_LENGTH, the focal length is used.
      *
      * It is possible to convert the aperture mode into field of view or vice versa using functions ComputeFieldOfView and
      * ComputeFocalLength. These functions use the camera aperture width and height for their computation.
      */
    //@{

    /** \enum ECameraApertureFormat Camera aperture formats.
      * - \e eCUSTOM_APERTURE_FORMAT
      * - \e e16MM_THEATRICAL
      * - \e eSUPER_16MM
      * - \e e35MM_ACADEMY
      * - \e e35MM_TV_PROJECTION
      * - \e e35MM_FULL_APERTURE
      * - \e e35MM_185_PROJECTION
      * - \e e35MM_ANAMORPHIC
      * - \e e70MM_PROJECTION
      * - \e eVISTAVISION
      * - \e eDYNAVISION
      * - \e eIMAX
      */
    typedef enum
    {
        eCUSTOM_APERTURE_FORMAT = 0,
        e16MM_THEATRICAL,
        eSUPER_16MM,
        e35MM_ACADEMY,
        e35MM_TV_PROJECTION,
        e35MM_FULL_APERTURE,
        e35MM_185_PROJECTION,
        e35MM_ANAMORPHIC,
        e70MM_PROJECTION,
        eVISTAVISION,
        eDYNAVISION,
        eIMAX
    } ECameraApertureFormat;

    /** Set the camera aperture format.
      * \param pFormat     The camera aperture format identifier.
      * \remarks           Changing the aperture format modifies the aperture width, height, and squeeze ratio accordingly.
      */
    void SetApertureFormat(ECameraApertureFormat pFormat);

    /** Get the camera aperture format.
      * \return     The camera's current aperture format identifier.
      */
    ECameraApertureFormat GetApertureFormat() const;

    /** \enum ECameraApertureMode
      * Camera aperture modes. The aperture mode determines which values drive the camera aperture. If the aperture mode is \e eHORIZONTAL_AND_VERTICAL,
      * \e eHORIZONTAL, or \e eVERTICAL, then the field of view is used. If the aperture mode is \e eFOCAL_LENGTH, then the focal length is used.
      * - \e eHORIZONTAL_AND_VERTICAL
      * - \e eHORIZONTAL
      * - \e eVERTICAL
      * - \e eFOCAL_LENGTH
      */
    typedef enum
    {
        eHORIZONTAL_AND_VERTICAL,
        eHORIZONTAL,
        eVERTICAL,
        eFOCAL_LENGTH
    } ECameraApertureMode;

    /** Set the camera aperture mode.
      * \param pMode     The camera aperture mode identifier.
      */
    void SetApertureMode(ECameraApertureMode pMode);

    /** Get the camera aperture mode.
      * \return     The camera's current aperture mode identifier.
      */
    ECameraApertureMode GetApertureMode() const;

    /** Set the camera aperture width in inches.
      * \param pWidth     The aperture width value.
      * \remarks          Must be a positive value. The minimum accepted value is 0.0001.
      *                   Changing the aperture width sets the camera aperture format to eCUSTOM_FORMAT.
      */
    void SetApertureWidth(double pWidth);

    /** Get the camera aperture width in inches.
      * \return     The camera's current aperture width value in inches.
      */
    double GetApertureWidth() const;

    /** Set the camera aperture height in inches.
      * \param pHeight     The aperture height value.
      * \remarks           Must be a positive value. The minimum accepted value is 0.0001.
      *                    Changing the aperture height sets the camera aperture format to eCUSTOM_FORMAT.
      */
    void SetApertureHeight(double pHeight);

    /** Get the camera aperture height in inches.
      * \return     The camera's current aperture height value in inches.
      */
    double GetApertureHeight() const;

    /** Set the squeeze ratio.
      * \param pRatio      The squeeze ratio value.
      * \remarks           Must be a positive value. The minimum accepted value is 0.0001.
      *                    Changing the squeeze ratio sets the camera aperture format to eCUSTOM_FORMAT.
      */
    void SetSqueezeRatio(double pRatio);

    /** Get the camera squeeze ratio.
      * \return     The camera's current squeeze ratio value.
      */
    double GetSqueezeRatio() const;

    /** \enum ECameraGateFit
      * Camera gate fit modes.
      * - \e eNO_FIT            No resolution gate fit.
      * - \e eVERTICAL_FIT      Fit the resolution gate vertically within the film gate.
      * - \e eHORIZONTAL_FIT    Fit the resolution gate horizontally within the film gate.
      * - \e eFILL_FIT          Fit the resolution gate within the film gate.
      * - \e eOVERSCAN_FIT      Fit the film gate within the resolution gate.
      * - \e eSTRETCH_FIT       Fit the resolution gate to the film gate.
      */
    typedef enum
    {
        eNO_FIT,
        eVERTICAL_FIT,
        eHORIZONTAL_FIT,
        eFILL_FIT,
        eOVERSCAN_FIT,
        eSTRETCH_FIT
    } ECameraGateFit;

    /** Compute the angle of view based on the given focal length, the aperture width, and aperture height.
      * \param pFocalLength     The focal length in millimeters
      * \return                 The computed angle of view in degrees
      */
    double ComputeFieldOfView(double pFocalLength) const;

    /** Compute the focal length based on the given angle of view, the aperture width, and aperture height.
      * \param pAngleOfView     The angle of view in degrees
      * \return                 The computed focal length in millimeters
      */
    double ComputeFocalLength(double pAngleOfView) const;

    /** \enum ECameraFilmRollOrder 
      * Specifies how the roll is applied with respect to the pivot value.
      * - \e eROTATE_TRANSLATE              The film back is first rotated then translated by the pivot point value.
      * - \e eHORIZONTAL_AND_VERTICAL       The film back is first translated then rotated by the film roll value.
      */
    typedef enum
    {
        eROTATE_TRANSLATE,
        eTRANSLATE_ROTATE
    } ECameraFilmRollOrder;

    //@}

    /**
      * \name BackPlane/FrontPlane and Plate Functions
	  * 
	  * In the FbxSdk terminology, the Back/Front plane is the support of the plate. And the plate is
	  * the support of the texture used for backgrounds/foregrounds. Functions and properties 
	  * identified by the "Plate" name are affecting the display of the texture on the plate. 
	  * The functions and properties identified with the "Back/FrontPlane" are affecting the plate.
	  *
	  * Typically a client application would place the BackPlate a small distance in front of the 
	  * FarPlane and the FrontPlate just behind the NearPlane to avoid them to be hidden by the clipping.
	  * Unless otherwise noted, there are no restrictions on the values stored by the camera object
	  * therefore it is the responsibility of the client application to process the information in a 
	  * meaningful way and to maintain consistency between the different properties relationships.
      */
    //@{

    /** Set the associated background image file.
      * \param pFileName     The path of the background image file.
      * \remarks             The background image file name must be valid.
	  * \remarks             This method is still provided for legacy files (Fbx version 5.0 and earlier)
	  *                      and must not be used in any other cases.
      */
    void SetBackgroundFileName(const char* pFileName);

    /** Get the background image file name.
      * \return     Pointer to the background filename string or \c NULL if not set.
	  * \remarks             This method is still provided for legacy files (Fbx version 5.0 and earlier)
	  *                      and must not be used in any other cases.
      */
    char const* GetBackgroundFileName() const;

    /** Set the media name associated to the background image file.
      * \param pFileName     The media name of the background image file.
      * \remarks             The media name is a unique name used to identify the background image file.
	  * \remarks             This method is still provided for legacy files (Fbx version 5.0 and earlier)
	  *                      and must not be used in any other cases.
      */
    void SetBackgroundMediaName(const char* pFileName);

    /** Get the media name associated to the background image file.
      * \return     Pointer to the media name string or \c NULL if not set.
	  * \remarks             This method is still provided for legacy files (Fbx version 5.0 and earlier)
	  *                      and must not be used in any other cases.
      */
    char const* GetBackgroundMediaName() const;

    /** Set the associated foreground image file.
    * \param pFileName     The path of the foreground image file.
    * \remarks             The foreground image file name must be valid.
    * \remarks             This method is still provided for legacy files (Fbx version 5.0 and earlier)
    *                      and must not be used in any other cases.
    */
    void SetForegroundFileName(const char* pFileName);

    /** Get the foreground image file name.
    * \return     Pointer to the foreground filename string or \c NULL if not set.
    * \remarks             This method is still provided for legacy files (Fbx version 5.0 and earlier)
    *                      and must not be used in any other cases.
    */
    char const* GetForegroundFileName() const;

    /** Set the media name associated to the foreground image file.
    * \param pFileName     The media name of the foreground image file.
    * \remarks             The media name is a unique name used to identify the foreground image file.
    * \remarks             This method is still provided for legacy files (Fbx version 5.0 and earlier)
    *                      and must not be used in any other cases.
    */
    void SetForegroundMediaName(const char* pFileName);

    /** Get the media name associated to the foreground image file.
    * \return     Pointer to the media name string or \c NULL if not set.
    * \remarks             This method is still provided for legacy files (Fbx version 5.0 and earlier)
    *                      and must not be used in any other cases.
    */
    char const* GetForegroundMediaName() const;

	
	/** \enum ECameraPlateDrawingMode Image plate drawing modes.
      * - \e eBACKGROUND                 Image is drawn behind models.
      * - \e eFOREGROUND                 Image is drawn in front of models based on alpha channel.
      * - \e eBACKGROUND_AND_FOREGROUND  Image is drawn behind and in front of models depending on alpha channel.
      */
    typedef enum
    {
        eBACKGROUND,
        eFOREGROUND,
        eBACKGROUND_AND_FOREGROUND
    } ECameraPlateDrawingMode;

	/** Set back plate matte threshold.
      * \param pThreshold     Threshold value on a range from 0.0 to 1.0.
      * \remarks              This option is only relevant if the background drawing mode is set to eFOREGROUND or eBACKGROUND_AND_FOREGROUND.
      *
      */
    void SetBackgroundAlphaTreshold(double pThreshold);

    /** Get back plate matte threshold.
      * \return      Threshold value on a range from 0.0 to 1.0.
      * \remarks     This option is only relevant if the background drawing mode is set to eFOREGROUND or eBACKGROUND_AND_FOREGROUND.
      *
      */
    double GetBackgroundAlphaTreshold() const;


	/** Change the back plate fit image flag.
      * \param pFitImage    New value for the BackPlateFitImage property.
      */
    void SetFitImage(bool pFitImage);

    /** Get the current back plate image flag.
      * \return             The value of the BackPlateFitImage property.
      */
    bool GetFitImage() const;

    /** Change the back plate crop flag.
      * \param pCrop          New value for the BackPlateCrop property.
      */
    void SetCrop(bool pCrop);

    /** Get the current back plate crop flag.
      * \return               The value of the BackPlateCrop property.
      */
    bool GetCrop() const;

    /** Change the back plate center flag.
      * \param pCenter        New value for the BackPlateCenter property.
      */
    void SetCenter(bool pCenter);

    /** Get the current back plate center flag.
      * \return               The value of the BackPlateCenter property.
      */
    bool GetCenter() const;

    /** Change the back plate keep ratio flag.
      * \param pKeepRatio     New value for the BackPlateKeepRatio property.
      */
    void SetKeepRatio(bool pKeepRatio);

    /** Get the current back plate keep ratio flag.
      * \return               The value of the BackPlateKeepRatio property.
      */
    bool GetKeepRatio() const;


	/** Enable or disable the display of the texture without the need to disconnect it from its plate.
      * \param pEnable     If \c true the texture is displayed.
      * \remarks           It is the responsibility of the client application to perform the required tasks according to the state
      *                    of this flag.
      */
    void SetShowFrontPlate(bool pEnable);

	/** Get the current state of the flag.
      * \return            \c true if show front plate is enabled, otherwise \c false.
      * \remarks           It is the responsibility of the client application to perform the required tasks according to the state
      *                    of this flag.
      */
    bool GetShowFrontPlate() const;
 
	/** Change the front plate fit image flag.
      * \param pFrontPlateFitImage	  New value for the FrontPlateFitImage property.
      */
    void SetFrontPlateFitImage(bool pFrontPlateFitImage);

    /** Get the current front plate fit image flag.
      * \return               The value of the BackPlateFitImage property.
      */
    bool GetFrontPlateFitImage() const;

    /** Change the front plate crop flag.
      * \param pFrontPlateCrop          New value for the FrontPlateCrop property.
      */
    void SetFrontPlateCrop(bool pFrontPlateCrop);

    /** Get the current front plate crop flag.
      * \return               The value of the FrontPlateCrop property.
      */
    bool GetFrontPlateCrop() const;

    /** Change the front plate center flag.
      * \param pFrontPlateCenter		  New value for the FrontPlateCenter property.
      */
    void SetFrontPlateCenter(bool pFrontPlateCenter);

    /** Get the current front plate center flag.
      * \return               The value of the FrontPlateCenter property.
      */
    bool GetFrontPlateCenter() const;

    /** Change the front plate keep ratio flag.
      * \param pFrontPlateKeepRatio     New value for the FrontPlateKeepRatio property.
      */
    void SetFrontPlateKeepRatio(bool pFrontPlateKeepRatio);

    /** Get the current front plate keep ratio flag.
      * \return               The value of the FrontPlateKeepRatio property.
      */
    bool GetFrontPlateKeepRatio() const;

    /** Set the front plate opacity value.
      * \param pOpacity       New value for the ForegroundOpacity property.
      */
    void SetForegroundOpacity(double pOpacity);

    /** Get the front plate opacity value.
      * \return               The value of the ForegroundOpacity property.
      */
    double GetForegroundOpacity() const;

    /** Attach the texture to the Front plate.
      * \param pTexture       New texture handle.
      */
    void SetForegroundTexture(KFbxTexture* pTexture);

    /** Get the texture connected to the Front plate.
      * \return KFbxTexture* The texture handle.
      */
    KFbxTexture* GetForegroundTexture() const;


	/** ECameraFrontBackPlaneDistanceMode Front and BackPlane distance modes.
      * - \e eRELATIVE_TO_INTEREST
      * - \e eRELATIVE_TO_CAMERA
      */
    typedef enum
    {
        eRELATIVE_TO_INTEREST,
        eRELATIVE_TO_CAMERA
    } ECameraFrontBackPlaneDistanceMode;

	/** Set the back plane distance mode flag.
      * \param pMode    The back plane distance mode flag.
      */
    void SetBackPlaneDistanceMode(ECameraFrontBackPlaneDistanceMode pMode);

    /** Get the back plane distance mode.
      * \return     Return the back plane distance mode identifier.
      */
    ECameraFrontBackPlaneDistanceMode GetBackPlaneDistanceMode() const;

	/** Set the front plane distance from the camera. The the absolute position of the plane must be calculated
	  * by taking into consideration the FrontPlaneDistanceMode.
      * \param pDistance    The front plane distance value.
	  * \remarks			It is the responsibility of the client application to ensure that this plane position is 
	  *                     within the frustum boundaries.
      */
    void SetFrontPlaneDistance(double pDistance);

    /** Get the front plane distance value.
      * \return double      The front plane distance value.
      */
    double GetFrontPlaneDistance() const;

    /** Set the front plane distance mode flag.
      * \param pMode        The front plane distance mode flag.
      */
    void SetFrontPlaneDistanceMode(ECameraFrontBackPlaneDistanceMode pMode);

    /** Get the front plane distance mode flag.
      * \return ECameraFrontBackPlaneDistanceMode    The front plane distance mode value.
      */
    ECameraFrontBackPlaneDistanceMode GetFrontPlaneDistanceMode() const;

    /** \enum ECameraFrontBackPlaneDisplayMode Front/Back Plane display modes.
      * - \e eDISABLED
      * - \e eALWAYS
      * - \e eWHEN_MEDIA
      */
    typedef enum
    {
        eDISABLED,
        eALWAYS,
        eWHEN_MEDIA
    } ECameraFrontBackPlaneDisplayMode;
	
    /** Set the front plane display mode. This mode can be used by the client application to
	  * decide under which circumstance the front plane should be drawn in the viewport.
	  *
      * \param pMode        The view frustum front plane display mode.
      */
    void SetViewFrustumFrontPlaneMode(ECameraFrontBackPlaneDisplayMode pMode);

    /** Get the front plane display mode.
      * \return ECameraFrontBackPlaneDisplayMode    The view frustum front plane display mode value.
      */
    ECameraFrontBackPlaneDisplayMode GetViewFrustumFrontPlaneMode() const;

    /** Set the back plane display mode. This mode can be used by the client application to
	  * decide under which circumstance the back plane should be drawn in the viewport.
	  *
      * \param pMode        The view frustum back plane display mode.
      */
    void SetViewFrustumBackPlaneMode(ECameraFrontBackPlaneDisplayMode pMode);

    /** Get the back plane display mode.
      * \return ECameraFrontBackPlaneDisplayMode    The view frustum back plane display mode value.
      */
    ECameraFrontBackPlaneDisplayMode GetViewFrustumBackPlaneMode() const;
    
    //@}

    /**
      * \name Camera View Functions
      * It is the responsibility of the client application to perform the required tasks according to the state
      * of the options that are either set or returned by these methods.
      */
    //@{

    /** Change the camera interest visibility flag.
      * \param pEnable     Set to \c true if the camera interest is shown.
      */
    void SetViewCameraInterest(bool pEnable);

    /** Get current visibility state of the camera interest.
      * \return     \c true if the camera interest is shown, or \c false if hidden.
      */
    bool GetViewCameraInterest() const;

	/** Change the camera near and far planes visibility flag.
      * \param pEnable      Set to \c true if the near and far planes are shown.
      */
    void SetViewNearFarPlanes(bool pEnable);

    /** Get current visibility state of the camera near and far planes.
      * \return     \c true if the near and far planes are shown.
      */
    bool GetViewNearFarPlanes() const;

    /** \enum ECameraSafeAreaStyle Camera safe area display styles.
      * - \e eROUND
      * - \e eSQUARE
      */
    typedef enum
    {
        eROUND = 0,
        eSQUARE = 1
    } ECameraSafeAreaStyle;

    //@}

    /**
      * \name Render Functions
      * It is the responsibility of the client application to perform the required tasks according to the state
      * of the options that are either set or returned by these methods.
      */
    //@{

    /** \enum ECameraRenderOptionsUsageTime Render options usage time.
      * - \e eINTERACTIVE
      * - \e eAT_RENDER
      */
    typedef enum
    {
        eINTERACTIVE,
        eAT_RENDER
    } ECameraRenderOptionsUsageTime;

    /** \enum ECameraAntialiasingMethod Antialiasing methods.
      * - \e eOVERSAMPLING_ANTIALIASING
      * - \e eHARDWARE_ANTIALIASING
      */
    typedef enum
    {
        eOVERSAMPLING_ANTIALIASING,
        eHARDWARE_ANTIALIASING
    } ECameraAntialiasingMethod;

    /** \enum ECameraSamplingType Oversampling types.
      * - \e eUNIFORM
      * - \e eSTOCHASTIC
      */
    typedef enum
    {
        eUNIFORM,
        eSTOCHASTIC
    } ECameraSamplingType;

    /** \enum ECameraFocusDistanceSource Camera focus sources.
      * - \e eCAMERA_INTEREST
      * - \e eSPECIFIC_DISTANCE
      */
    typedef enum
    {
        eCAMERA_INTEREST,
        eSPECIFIC_DISTANCE
    } ECameraFocusDistanceSource;

    //@}

    /**
      * \name Utility Functions.
      */
    //@{

    /** Determine if the given bounding box is in the camera's view. 
      * The input points do not need to be ordered in any particular way.
      * \param pWorldToScreen The world to screen transformation. See ComputeWorldToScreen.
      * \param pWorldToCamera The world to camera transformation. 
               Inverse matrix returned from KFbxAnimEvaluator::GetNodeGlobalTransform or KFbxAnimEvaluator::GetNodeGlobalTransformFast is suitable.
               See KFbxScene::GetEvaluator and KFbxAnimEvaluator::GetNodeGlobalTransform or KFbxAnimEvaluator::GetNodeGlobalTransformFast.
      * \param pPoints 8 corners of the bounding box.
      * \return true if any of the given points are in the camera's view, false otherwise.
      */
    bool IsBoundingBoxInView( const KFbxMatrix& pWorldToScreen, 
                             const KFbxMatrix& pWorldToCamera, 
                             const KFbxVector4 pPoints[8] ) const;

    /** Determine if the given 3d point is in the camera's view. 
      * \param pWorldToScreen The world to screen transformation. See ComputeWorldToScreen.
      * \param pWorldToCamera The world to camera transformation. 
               Inverse matrix returned from KFbxAnimEvaluator::GetNodeGlobalTransform or KFbxAnimEvaluator::GetNodeGlobalTransformFast is suitable.
               See KFbxScene::GetEvaluator and KFbxAnimEvaluator::GetNodeGlobalTransform or KFbxAnimEvaluator::GetNodeGlobalTransformFast.
      * \param pPoint World-space point to test.
      * \return true if the given point is in the camera's view, false otherwise.
      */
    bool IsPointInView( const KFbxMatrix& pWorldToScreen, const KFbxMatrix& pWorldToCamera, const KFbxVector4& pPoint ) const;

    /** Compute world space to screen space transformation matrix.
      * \param pPixelHeight The pixel height of the output image.
      * \param pPixelWidth The pixel height of the output image.
      * \param pWorldToCamera The world to camera affine transformation matrix.
      * \return The world to screen space matrix, or the identity matrix on error.
      */
    KFbxMatrix ComputeWorldToScreen(int pPixelWidth, int pPixelHeight, const KFbxXMatrix& pWorldToCamera) const;

    /** Compute the perspective matrix for this camera. 
      * Suitable for transforming camera space to normalized device coordinate space.
      * Also suitable for use as an OpenGL projection matrix. Note this fails if the
      * ProjectionType is not ePERSPECTIVE. 
      * \param pPixelHeight The pixel height of the output image.
      * \param pPixelWidth The pixel width of the output image.
      * \param pIncludePostPerspective Indicate that post-projection transformations (offset, roll) 
      *        be included in the output matrix.
      * \return A perspective matrix, or the identity matrix on error.
      */
    KFbxMatrix ComputePerspective( int pPixelWidth, int pPixelHeight, bool pIncludePostPerspective ) const;

    //@}

    //////////////////////////////////////////////////////////////////////////
    //
    // Properties
    //
    //////////////////////////////////////////////////////////////////////////

    // -----------------------------------------------------------------------
    // Geometrical
    // -----------------------------------------------------------------------

    /** This property handles the camera position (XYZ coordinates).
      *
      * To access this property do: Position.Get().
      * To set this property do: Position.Set(fbxDouble3).
      *
      * \remarks Default Value is (0.0, 0.0, 0.0)
      */
    KFbxTypedProperty<fbxDouble3>                       Position;

    /** This property handles the camera Up Vector (XYZ coordinates).
      *
      * To access this property do: UpVector.Get().
      * To set this property do: UpVector.Set(fbxDouble3).
      *
      * \remarks Default Value is (0.0, 1.0, 0.0)
      */
    KFbxTypedProperty<fbxDouble3>                       UpVector;

    /** This property handles the default point (XYZ coordinates) the camera is looking at.
      *
      * To access this property do: InterestPosition.Get().
      * To set this property do: InterestPosition.Set(fbxDouble3).
      *
      * \remarks During the computations of the camera position
      * and orientation, this property is overridden by the
      * position of a valid target in the parent node.
      *
      * \remarks Default Value is (0.0, 0.0, 0.0)
      */
    KFbxTypedProperty<fbxDouble3>                       InterestPosition;

    /** This property handles the camera roll angle in degree(s).
      *
      * To access this property do: Roll.Get().
      * To set this property do: Roll.Set(fbxDouble1).
      *
      * Default value is 0.
      */
    KFbxTypedProperty<fbxDouble1>                       Roll;

    /** This property handles the camera optical center X, in pixels.
      * It parameter sets the optical center horizontal offset when the
      * camera aperture mode is set to \e eHORIZONTAL_AND_VERTICAL. It
      * has no effect otherwise.
      *
      * To access this property do: OpticalCenterX.Get().
      * To set this property do: OpticalCenterX.Set(fbxDouble1).
      *
      * Default value is 0.
      */
    KFbxTypedProperty<fbxDouble1>                       OpticalCenterX;

    /** This property handles the camera optical center Y, in pixels.
      * It sets the optical center horizontal offset when the
      * camera aperture mode is set to \e eHORIZONTAL_AND_VERTICAL. This
      * parameter has no effect otherwise.
      *
      * To access this property do: OpticalCenterY.Get().
      * To set this property do: OpticalCenterY.Set(fbxDouble1).
      *
      * Default value is 0.
      */
    KFbxTypedProperty<fbxDouble1>                       OpticalCenterY;

    /** This property handles the camera RGB values of the background color.
      *
      * To access this property do: BackgroundColor.Get().
      * To set this property do: BackgroundColor.Set(fbxDouble3).
      *
      * Default value is black (0, 0, 0)
      */
    KFbxTypedProperty<fbxDouble3>                       BackgroundColor;

    /** This property handles the camera turn table angle in degree(s)
      *
      * To access this property do: TurnTable.Get().
      * To set this property do: TurnTable.Set(fbxDouble1).
      *
      * Default value is 0.
      */
    KFbxTypedProperty<fbxDouble1>                       TurnTable;

    /** This property handles a flags that indicates if the camera displays the
      * Turn Table icon or not.
      *
      * To access this property do: DisplayTurnTableIcon.Get().
      * To set this property do: DisplayTurnTableIcon.Set(fbxBool1).
      *
      * Default value is false (no display).
      */
    KFbxTypedProperty<fbxBool1>                         DisplayTurnTableIcon;

    // -----------------------------------------------------------------------
    // Motion Blur
    // -----------------------------------------------------------------------

    /** This property handles a flags that indicates if the camera uses
      * motion blur or not.
      *
      * To access this property do: UseMotionBlur.Get().
      * To set this property do: UseMotionBlur.Set(fbxBool1).
      *
      * Default value is false (do not use motion blur).
      */
    KFbxTypedProperty<fbxBool1>                         UseMotionBlur;

    /** This property handles a flags that indicates if the camera uses
      * real time motion blur or not.
      *
      * To access this property do: UseRealTimeMotionBlur.Get().
      * To set this property do: UseRealTimeMotionBlur.Set(fbxBool1).
      *
      * Default value is false (use real time motion blur).
      */
    KFbxTypedProperty<fbxBool1>                         UseRealTimeMotionBlur;

    /** This property handles the camera motion blur intensity (in pixels).
      *
      * To access this property do: MotionBlurIntensity.Get().
      * To set this property do: MotionBlurIntensity.Set(fbxDouble1).
      *
      * Default value is 1.
      */
    KFbxTypedProperty<fbxDouble1>                       MotionBlurIntensity;

    // -----------------------------------------------------------------------
    // Optical
    // -----------------------------------------------------------------------

    /** This property handles the camera aspect ratio mode.
      *
      * \remarks This Property is in a Read Only mode.
      * \remarks Please use function SetAspect() if you want to change its value.
      *
      * Default value is eWINDOW_SIZE.
      *
      */
    KFbxTypedProperty<ECameraAspectRatioMode>           AspectRatioMode;

    /** This property handles the camera aspect width.
      *
      * \remarks This Property is in a Read Only mode.
      * \remarks Please use function SetAspect() if you want to change its value.
      *
      * Default value is 320.
      */
    KFbxTypedProperty<fbxDouble1>                       AspectWidth;

    /** This property handles the camera aspect height.
      *
      * \remarks This Property is in a Read Only mode.
      * \remarks Please use function SetAspect() if you want to change its value.
      *
      * Default value is 200.
      */
    KFbxTypedProperty<fbxDouble1>                       AspectHeight;

    /** This property handles the pixel aspect ratio.
      *
      * \remarks This Property is in a Read Only mode.
      * \remarks Please use function SetPixelRatio() if you want to change its value.
      * Default value is 1.
      * \remarks Value range is [0.050, 20.0].
      */
    KFbxTypedProperty<fbxDouble1>                       PixelAspectRatio;

    /** This property handles the aperture mode.
      *
      * Default value is eVERTICAL.
      */
    KFbxTypedProperty<ECameraApertureMode>              ApertureMode;

    /** This property handles the gate fit mode.
      *
      * To access this property do: GateFit.Get().
      * To set this property do: GateFit.Set(ECameraGateFit).
      *
      * Default value is eNO_FIT.
      */
    KFbxTypedProperty<ECameraGateFit>                   GateFit;

    /** This property handles the field of view in degrees.
      *
      * To access this property do: FieldOfView.Get().
      * To set this property do: FieldOfView.Set(fbxDouble1).
      *
      * \remarks This property has meaning only when
      * property ApertureMode equals eHORIZONTAL or eVERTICAL.
      *
      * \remarks Default value is 40.
      * \remarks Value range is [1.0, 179.0].
      */
    KFbxTypedProperty<fbxDouble1>                       FieldOfView;


    /** This property handles the X (horizontal) field of view in degrees.
      *
      * To access this property do: FieldOfViewX.Get().
      * To set this property do: FieldOfViewX.Set(fbxDouble1).
      *
      * \remarks This property has meaning only when
      * property ApertureMode equals eHORIZONTAL or eVERTICAL.
      *
      * Default value is 1.
      * \remarks Value range is [1.0, 179.0].
      */
    KFbxTypedProperty<fbxDouble1>                       FieldOfViewX;

    /** This property handles the Y (vertical) field of view in degrees.
      *
      * To access this property do: FieldOfViewY.Get().
      * To set this property do: FieldOfViewY.Set(fbxDouble1).
      *
      * \remarks This property has meaning only when
      * property ApertureMode equals eHORIZONTAL or eVERTICAL.
      *
      * \remarks Default value is 1.
      * \remarks Value range is [1.0, 179.0].
      */
    KFbxTypedProperty<fbxDouble1>                       FieldOfViewY;

    /** This property handles the focal length (in millimeters).
      *
      * To access this property do: FocalLength.Get().
      * To set this property do: FocalLength.Set(fbxDouble1).
      *
      * Default value is the result of ComputeFocalLength(40.0).
      */
    KFbxTypedProperty<fbxDouble1>                       FocalLength;

    /** This property handles the camera format.
      *
      * To access this property do: CameraFormat.Get().
      * To set this property do: CameraFormat.Set(ECameraFormat).
      *
      * \remarks This Property is in a Read Only mode.
      * \remarks Please use function SetFormat() if you want to change its value.
      * Default value is eCUSTOM_FORMAT.
      */
    KFbxTypedProperty<ECameraFormat>                    CameraFormat;

    // -----------------------------------------------------------------------
    // Frame
    // -----------------------------------------------------------------------

    /** This property stores a flag that indicates to use or not a color for
      * the frame.
      *
      * To access this property do: UseFrameColor.Get().
      * To set this property do: UseFrameColor.Set(fbxBool1).
      *
      * Default value is false.
      */
    KFbxTypedProperty<fbxBool1>                         UseFrameColor;

    /** This property handles the frame color
      *
      * To access this property do: FrameColor.Get().
      * To set this property do: FrameColor.Set(fbxDouble3).
      *
      * Default value is (0.3, 0.3, 0.3).
      */
    KFbxTypedProperty<fbxDouble3>                       FrameColor;

    // -----------------------------------------------------------------------
    // On Screen Display
    // -----------------------------------------------------------------------

    /** This property handles the show name flag.
      *
      * To access this property do: ShowName.Get().
      * To set this property do: ShowName.Set(fbxBool1).
      *
      * Default value is true.
      */
    KFbxTypedProperty<fbxBool1>                         ShowName;

    /** This property handles the show info on moving flag.
      *
      * To access this property do: ShowInfoOnMoving.Get().
      * To set this property do: ShowInfoOnMoving.Set(fbxBool1).
      *
      * Default value is true.
      */
    KFbxTypedProperty<fbxBool1>                         ShowInfoOnMoving;

    /** This property handles the draw floor grid flag
      *
      * To access this property do: ShowGrid.Get().
      * To set this property do: ShowGrid.Set(fbxBool1).
      *
      * Default value is true.
      */
    KFbxTypedProperty<fbxBool1>                         ShowGrid;

    /** This property handles the show optical center flag
      *
      * To access this property do: ShowOpticalCenter.Get().
      * To set this property do: ShowOpticalCenter.Set(fbxBool1).
      *
      * Default value is false.
      */
    KFbxTypedProperty<fbxBool1>                         ShowOpticalCenter;

    /** This property handles the show axis flag
      *
      * To access this property do: ShowAzimut.Get().
      * To set this property do: ShowAzimut.Set(fbxBool1).
      *
      * Default value is true.
      */
    KFbxTypedProperty<fbxBool1>                         ShowAzimut;

    /** This property handles the show time code flag
      *
      * To access this property do: ShowTimeCode.Get().
      * To set this property do: ShowTimeCode.Set(fbxBool1).
      *
      * Default value is true.
      */
    KFbxTypedProperty<fbxBool1>                         ShowTimeCode;

    /** This property handles the show audio flag
      *
      * To access this property do: ShowAudio.Get().
      * To set this property do: ShowAudio.Set(fbxBool1).
      *
      * Default value is false.
      */
    KFbxTypedProperty<fbxBool1>                         ShowAudio;

    /** This property handles the show audio flag
      *
      * To access this property do: AudioColor.Get().
      * To set this property do: AudioColor.Set(fbxDouble3).
      *
      * Default value is (0.0, 1.0, 0.0)
      */
    KFbxTypedProperty<fbxDouble3>                       AudioColor;

    // -----------------------------------------------------------------------
    // Clipping Planes
    // -----------------------------------------------------------------------

    /** This property handles the near plane distance.
      *
      * \remarks This Property is in a Read Only mode.
      * \remarks Please use function SetNearPlane() if you want to change its value.
      * Default value is 10.
      * \remarks Value range is [0.001, 600000.0].
      */
    KFbxTypedProperty<fbxDouble1>                       NearPlane;

    /** This property handles the far plane distance.
      *
      * \remarks This Property is in a Read Only mode
      * \remarks Please use function SetFarPlane() if you want to change its value
      * Default value is 4000
      * \remarks Value range is [0.001, 600000.0]
      */
    KFbxTypedProperty<fbxDouble1>                       FarPlane;


    /** This property indicates that the clip planes should be automatically computed.
      *
      * To access this property do: AutoComputeClipPlanes.Get().
      * To set this property do: AutoComputeClipPlanes.Set(fbxBool1).
      *
      * When this property is set to true, the NearPlane and FarPlane values are
      * ignored. Note that not all applications support this flag.
      */
    KFbxTypedProperty<fbxBool1>                         AutoComputeClipPlanes;


    // -----------------------------------------------------------------------
    // Camera Film Setting
    // -----------------------------------------------------------------------

    /** This property handles the film aperture width (in inches).
      *
      * \remarks This Property is in a Read Only mode
      * \remarks Please use function SetApertureWidth()
      * or SetApertureFormat() if you want to change its value
      * Default value is 0.8160
      * \remarks Value range is [0.0001, +inf]
      */
    KFbxTypedProperty<fbxDouble1>                       FilmWidth;

    /** This property handles the film aperture height (in inches).
      *
      * \remarks This Property is in a Read Only mode
      * \remarks Please use function SetApertureHeight()
      * or SetApertureFormat() if you want to change its value
      * Default value is 0.6120
      * \remarks Value range is [0.0001, +inf]
      */
    KFbxTypedProperty<fbxDouble1>                       FilmHeight;

    /** This property handles the film aperture aspect ratio.
      *
      * \remarks This Property is in a Read Only mode
      * \remarks Please use function SetApertureFormat() if you want to change its value
      * Default value is (FilmWidth / FilmHeight)
      * \remarks Value range is [0.0001, +inf]
      */
    KFbxTypedProperty<fbxDouble1>                       FilmAspectRatio;

    /** This property handles the film aperture squeeze ratio.
      *
      * \remarks This Property is in a Read Only mode
      * \remarks Please use function SetSqueezeRatio()
      * or SetApertureFormat() if you want to change its value
      * Default value is 1.0
      * \remarks Value range is [0.0001, +inf]
      */
    KFbxTypedProperty<fbxDouble1>                       FilmSqueezeRatio;

    /** This property handles the film aperture format.
      *
      * \remarks This Property is in a Read Only mode
      * \remarks Please use function SetApertureFormat()
      * if you want to change its value
      * Default value is eCUSTOM_APERTURE_FORMAT
      */
    KFbxTypedProperty<ECameraApertureFormat>            FilmFormat;

    /** This property handles the horizontal offset from the center of the film aperture,
    * defined by the film height and film width. The offset is measured
    * in inches.
    *
    * To access this property do: FilmOffsetX.Get().
    * To set this property do: FilmOffsetX.Set(fbxDouble1).
    *
    */
    KFbxTypedProperty<fbxDouble1>                       FilmOffsetX;

    /** This property handles the vertical offset from the center of the film aperture,
    * defined by the film height and film width. The offset is measured
    * in inches.
    *
    * To access this property do: FilmOffsetY.Get().
    * To set this property do: FilmOffsetY.Set(fbxDouble1).
    *
    */
    KFbxTypedProperty<fbxDouble1>                       FilmOffsetY;

    /** This property handles the pre-scale value. 
      * The value is multiplied against the computed projection matrix. 
      * It is applied before the film roll. 
      * To access this property do: PreScale.Get().
      * To set this property do: PreScale.Set(fbxDouble1).
      * Default value is 1.0
      */
    KFbxTypedProperty<fbxDouble1>                       PreScale;

    /** This property handles the horizontal film horizontal translation.
      * To access this property do: FilmTranslateX.Get().
      * To set this property do: FilmTranslateX.Set(fbxDouble1).
      * Default value is 0.0
      */
    KFbxTypedProperty<fbxDouble1>                       FilmTranslateX;

    /** This property handles the vertical film translation.
    * To access this property do: FilmTranslateY.Get().
    * To set this property do: FilmTranslateY.Set(fbxDouble1).
    * Default value is 0.0
    */
    KFbxTypedProperty<fbxDouble1>                       FilmTranslateY;

    /** This property handles the horizontal pivot point used for rotating the film back.
      * To access this property do: FilmRollPivotX.Get().
      * To set this property do: FilmRollPivotX.Set(fbxDouble1).
      * Default value is 0.0
      * \remarks FilmRollPivot value is used to compute the film roll matrix, which is a component of the post projection matrix.
      */
    KFbxTypedProperty<fbxDouble1>                       FilmRollPivotX;

    /** This property handles the vertical pivot point used for rotating the film back.
    * To access this property do: FilmRollPivotY.Get().
    * To set this property do: FilmRollPivotY.Set(fbxDouble1).
    * Default value is 0.0
    * \remarks FilmRollPivot value is used to compute the film roll matrix, which is a component of the post projection matrix.
    */
    KFbxTypedProperty<fbxDouble1>                       FilmRollPivotY;

    /** This property handles the amount of rotation around the film back 
      * The roll value is specified in degrees.
      * To access this property do: FilmRollValue.Get().
      * To set this property do: FilmRollValue.Set(fbxDouble1).
      * Default value is 0.0
      * \remarks The rotation occurs around the specified pivot point, 
      * this value is used to compute a film roll matrix, which is a component of the post-projection matrix. 
      */
    KFbxTypedProperty<fbxDouble1>                       FilmRollValue;

    /** This property handles how the roll is applied with respect to the pivot value.
      * eROTATE_TRANSLATE    The film back is first rotated then translated by the pivot point value.
      * eTRANSLATE_ROTATE    The film back is first translated then rotated by the film roll value.
      * To access this property do: FilmRollOrder.Get().
      * To set this property do: FilmRollOrder.Set(ECameraFilmRollOrder).
      * Default value is eROTATE_TRANSLATE
      */
    KFbxTypedProperty<ECameraFilmRollOrder>             FilmRollOrder ;

    // -----------------------------------------------------------------------
    // Camera View Widget Option
    // -----------------------------------------------------------------------

    /** This property handles the view camera to look at flag.
      *
      * To access this property do: ViewCameraToLookAt.Get().
      * To set this property do: ViewCameraToLookAt.Set(fbxBool1).
      *
      * Default value is true
      */
    KFbxTypedProperty<fbxBool1>                         ViewCameraToLookAt;

	/** This property handles the view frustum near and far plane display state.
      *
      * To access this property do: ViewFrustumNearFarPlane.Get().
      * To set this property do: ViewFrustumNearFarPlane.Set(fbxBool1).
      *
      * Default value is false
      */
    KFbxTypedProperty<fbxBool1>                         ViewFrustumNearFarPlane;

    /** This property handles the view frustum back plane mode.
      *
      * To access this property do: ViewFrustumBackPlaneMode.Get().
      * To set this property do: ViewFrustumBackPlaneMode.Set(ECameraFrontBackPlaneDisplayMode).
      *
      * Default value is eWHEN_MEDIA
      */
    KFbxTypedProperty<ECameraFrontBackPlaneDisplayMode>	ViewFrustumBackPlaneMode;

    /** This property handles the view frustum back plane distance.
      *
      * To access this property do: BackPlaneDistance.Get().
      * To set this property do: BackPlaneDistance.Set(fbxDouble1).
      *
      * Default value is 100.0
      */
    KFbxTypedProperty<fbxDouble1>                       BackPlaneDistance;

    /** This property handles the view frustum back plane distance mode.
      *
      * To access this property do: BackPlaneDistanceMode.Get().
      * To set this property do: BackPlaneDistanceMode.Set(ECameraFrontBackPlaneDistanceMode).
      *
      * Default value is eRELATIVE_TO_INTEREST
      */
    KFbxTypedProperty<ECameraFrontBackPlaneDistanceMode>	BackPlaneDistanceMode;

    /** This property handles the view frustum front plane mode.
      *
      * To access this property do: ViewFrustumFrontPlaneMode.Get().
      * To set this property do: ViewFrustumFrontPlaneMode.Set(ECameraFrontBackPlaneDisplayMode).
      *
      * Default value is eWHEN_MEDIA
      */
    KFbxTypedProperty<ECameraFrontBackPlaneDisplayMode>	ViewFrustumFrontPlaneMode;

    /** This property handles the view frustum front plane distance.
      *
      * To access this property do: FrontPlaneDistance.Get().
      * To set this property do: FrontPlaneDistance.Set(fbxDouble1).
      *
      * Default value is 100.0
      */
    KFbxTypedProperty<fbxDouble1>                       FrontPlaneDistance;

    /** This property handles the view frustum front plane distance mode.
      *
      * To access this property do: FrontPlaneDistanceMode.Get().
      * To set this property do: FrontPlaneDistanceMode.Set(ECameraFrontBackPlaneDistanceMode).
      *
      * Default value is eRELATIVE_TO_INTEREST
      */
    KFbxTypedProperty<ECameraFrontBackPlaneDistanceMode>	FrontPlaneDistanceMode;

    // -----------------------------------------------------------------------
    // Camera Lock Mode
    // -----------------------------------------------------------------------

    /** This property handles the lock mode.
      *
      * To access this property do: LockMode.Get().
      * To set this property do: LockMode.Set(fbxBool1).
      *
      * Default value is false
      */
    KFbxTypedProperty<fbxBool1>                         LockMode;

    /** This property handles the lock interest navigation flag.
      *
      * To access this property do: LockInterestNavigation.Get().
      * To set this property do: LockInterestNavigation.Set(fbxBool1).
      *
      * Default value is false
      */
    KFbxTypedProperty<fbxBool1>                         LockInterestNavigation;

    // -----------------------------------------------------------------------
    // Background Image Display Options
    // -----------------------------------------------------------------------

    /** This property handles the fit image flag.
      *
      * To access this property do: FitImage.Get().
      * To set this property do: FitImage.Set(fbxBool1).
      *
      * Default value is false.
      */
    KFbxTypedProperty<fbxBool1>                         FitImage;

    /** This property handles the crop flag.
      *
      * To access this property do: Crop.Get().
      * To set this property do: Crop.Set(fbxBool1).
      *
      * Default value is false.
      */
    KFbxTypedProperty<fbxBool1>                         Crop;

    /** This property handles the center flag.
      *
      * To access this property do: Center.Get().
      * To set this property do: Center.Set(fbxBool1).
      *
      * Default value is true.
      */
    KFbxTypedProperty<fbxBool1>                         Center;

    /** This property handles the keep ratio flag.
      *
      * To access this property do: KeepRatio.Get().
      * To set this property do: KeepRatio.Set(fbxBool1).
      *
      * Default value is true.
      */
    KFbxTypedProperty<fbxBool1>                         KeepRatio;

    /** This property handles the background alpha threshold value.
      *
      * To access this property do: BackgroundAlphaTreshold.Get().
      * To set this property do: BackgroundAlphaTreshold.Set(fbxDouble1).
      *
      * Default value is 0.5.
      */
    KFbxTypedProperty<fbxDouble1>                       BackgroundAlphaTreshold;

    /** This property handles the back plane offset X
    *
    * To access this property do: BackPlaneOffsetX.Get()
    * To set this property do: BackPlaneOffsetX.Set(fbxDouble1).
    *
    * Default value is 0.
    */
    KFbxTypedProperty<fbxDouble1>                       BackPlaneOffsetX;

    /** This property handles the back plane offset Y
    *
    * To access this property do: BackPlaneOffsetY.Get()
    * To set this property do: BackPlaneOffsetY.Set(fbxDouble1).
    *
    * Default value is 0.
    */
    KFbxTypedProperty<fbxDouble1>                       BackPlaneOffsetY;

    /** This property handles the back plane rotation
      *
      * To access this property do: BackPlaneRotation.Get()
      * To set this property do: BackPlaneRotation.Set(fbxDouble1).
      *
      * Default value is 0.
      */
    KFbxTypedProperty<fbxDouble1>                       BackPlaneRotation;

    /** This property handles the back plane scaling X
    *
    * To access this property do: BackPlaneScaleX.Get()
    * To set this property do: BackPlaneScaleX.Set(fbxDouble1).
    *
    * Default value is 1.
    * \remarks The application manipulating the camera has to take into consideration
    * the KeepRatio value too.
    */
    KFbxTypedProperty<fbxDouble1>                       BackPlaneScaleX;

    /** This property handles the back plane scaling Y
    *
    * To access this property do: BackPlaneScaleY.Get()
    * To set this property do: BackPlaneScaleY.Set(fbxDouble1).
    *
    * Default value is 1.
    * \remarks The application manipulating the camera has to take into consideration
    * the KeepRatio value too.
    */
    KFbxTypedProperty<fbxDouble1>                       BackPlaneScaleY;

    /** This property handles the back plane show flag.
    *
    * To access this property do: ShowBackPlate.Get().
    * To set this property do: ShowBackPlate.Set(fbxBool1).
    *
    * Default value is false.
    * \remarks this replaces ForegroundTransparent 
    */
    KFbxTypedProperty<fbxBool1>                         ShowBackplate;

    /** This property has the background textures connected to it.
      *
      * To access this property do: BackgroundTexture.GetSrcObject().
      * To set this property do: BackgroundTexture.ConnectSrcObject(KFbxObject*).
      *
      * \remarks they are connected as source objects
      */
    KFbxTypedProperty<fbxReference> BackgroundTexture;


    // -----------------------------------------------------------------------
    // Foreground Image Display Options
    // -----------------------------------------------------------------------

    /** This property handles the fit image for front plate flag.
      *
      * To access this property do: FrontPlateFitImage.Get().
      * To set this property do: FrontPlateFitImage.Set(fbxBool1).
      *
      * Default value is false.
      */
    KFbxTypedProperty<fbxBool1> FrontPlateFitImage;

    /** This property handles the front plane crop flag.
      *
      * To access this property do: FrontPlateCrop.Get().
      * To set this property do: FrontPlateCrop.Set(fbxBool1).
      *
      * Default value is false.
      */
    KFbxTypedProperty<fbxBool1> FrontPlateCrop;

    /** This property handles the front plane center flag.
      *
      * To access this property do: FrontPlateCenter.Get().
      * To set this property do: FrontPlateCenter.Set(fbxBool1).
      *
      * Default value is true.
      */
    KFbxTypedProperty<fbxBool1> FrontPlateCenter;

    /** This property handles the front plane keep ratio flag.
      *
      * To access this property do: FrontPlateKeepRatio.Get().
      * To set this property do: FrontPlateKeepRatio.Set(fbxBool1).
      *
      * Default value is true.
      */
    KFbxTypedProperty<fbxBool1> FrontPlateKeepRatio;


    /** This property handles the front plane show flag.
      *
      * To access this property do: ShowFrontplate.Get().
      * To set this property do: ShowFrontplate.Set(fbxBool1).
      *
      * Default value is false.
      * \remarks this replaces ForegroundTransparent 
      */
    KFbxTypedProperty<fbxBool1> ShowFrontplate;

    /** This property handles the front plane offset X.
    *
    * To access this property do: FrontPlaneOffsetX.Get()
    * To set this property do: FrontPlaneOffsetX.Set(fbxDouble1).
    *
    * Default value is 0.
    */
    KFbxTypedProperty<fbxDouble1>                       FrontPlaneOffsetX;

    /** This property handles the front plane offset Y.
    *
    * To access this property do: FrontPlaneOffsetY.Get()
    * To set this property do: FrontPlaneOffsetY.Set(fbxDouble1).
    *
    * Default value is 0.
    */
    KFbxTypedProperty<fbxDouble1>                       FrontPlaneOffsetY;

    /** This property handles the front plane rotation.
      *
      * To access this property do: FrontPlaneRotation.Get()
      * To set this property do: FrontPlaneRotation.Set(fbxDouble1).
      *
      * Default value is 0.
      */
    KFbxTypedProperty<fbxDouble1>                       FrontPlaneRotation;

    /** This property handles the front plane scaling X.
    *
    * To access this property do: FrontPlaneScaleX.Get()
    * To set this property do: FrontPlaneScaleX.Set(fbxDouble1).
    *
    * Default value is 1.
    */
    KFbxTypedProperty<fbxDouble1>                       FrontPlaneScaleX;

    /** This property handles the front plane scaling Y.
    *
    * To access this property do: FrontPlaneScaleY.Get()
    * To set this property do: FrontPlaneScaleY.Set(fbxDouble1).
    *
    * Default value is 1.
    */
    KFbxTypedProperty<fbxDouble1>                       FrontPlaneScaleY;
	
	/** This property has the foreground textures connected to it.
      *
      * To access this property do: ForegroundTexture.GetSrcObject().
      * To set this property do: ForegroundTexture.ConnectSrcObject(KFbxObject*).
      *
      * \remarks they are connected as source objects
      */
    KFbxTypedProperty<fbxReference>						ForegroundTexture;

    /** This property handles the foreground image opacity value.
      *
      * To access this property do: ForegroundOpacity.Get().
      * To set this property do: ForegroundOpacity.Set(fbxDouble1).
      *
      * Default value is 1.0.
      */
    KFbxTypedProperty<fbxDouble1>						ForegroundOpacity;

    // -----------------------------------------------------------------------
    // Safe Area
    // -----------------------------------------------------------------------

    /** This property handles the display safe area flag.
      *
      * To access this property do: DisplaySafeArea.Get().
      * To set this property do: DisplaySafeArea.Set(fbxBool1).
      *
      * Default value is false
      */
    KFbxTypedProperty<fbxBool1>                         DisplaySafeArea;

    /** This property handles the display safe area on render flag.
      *
      * To access this property do: DisplaySafeAreaOnRender.Get().
      * To set this property do: DisplaySafeAreaOnRender.Set(fbxBool1).
      *
      * Default value is false
      */
    KFbxTypedProperty<fbxBool1>                         DisplaySafeAreaOnRender;

    /** This property handles the display safe area display style.
      *
      * To access this property do: SafeAreaDisplayStyle.Get().
      * To set this property do: SafeAreaDisplayStyle.Set(ECameraSafeAreaStyle).
      *
      * Default value is eSQUARE
      */
    KFbxTypedProperty<ECameraSafeAreaStyle>             SafeAreaDisplayStyle;

    /** This property handles the display safe area aspect ratio.
      *
      * To access this property do: SafeAreaDisplayStyle.Get().
      * To set this property do: SafeAreaAspectRatio.Set(fbxDouble1).
      *
      * Default value is 1.33333333333333
      */
    KFbxTypedProperty<fbxDouble1>                       SafeAreaAspectRatio;

    // -----------------------------------------------------------------------
    // 2D Magnifier
    // -----------------------------------------------------------------------

    /** This property handles the use 2d magnifier zoom flag.
      *
      * To access this property do: Use2DMagnifierZoom.Get().
      * To set this property do: Use2DMagnifierZoom.Set(fbxBool1).
      *
      * Default value is false
      */
    KFbxTypedProperty<fbxBool1>                         Use2DMagnifierZoom;

    /** This property handles the 2d magnifier zoom value.
      *
      * To access this property do: _2DMagnifierZoom.Get().
      * To set this property do: _2DMagnifierZoom.Set(fbxDouble1).
      *
      * Default value is 100.0
      */
    KFbxTypedProperty<fbxDouble1>                       _2DMagnifierZoom;

    /** This property handles the 2d magnifier X value.
      *
      * To access this property do: _2DMagnifierX.Get().
      * To set this property do: _2DMagnifierX.Set(fbxDouble1).
      *
      * Default value is 50.0
      */
    KFbxTypedProperty<fbxDouble1>                       _2DMagnifierX;

    /** This property handles the 2d magnifier Y value.
      *
      * To access this property do: _2DMagnifierY.Get().
      * To set this property do: _2DMagnifierY.Set(fbxDouble1).
      *
      * Default value is 50.0
      */
    KFbxTypedProperty<fbxDouble1>                       _2DMagnifierY;

    // -----------------------------------------------------------------------
    // Projection Type: Ortho, Perspective
    // -----------------------------------------------------------------------

    /** This property handles the projection type
      *
      * To access this property do: ProjectionType.Get().
      * To set this property do: ProjectionType.Set(ECameraProjectionType).
      *
      * Default value is ePERSPECTIVE.
      */
    KFbxTypedProperty<ECameraProjectionType>            ProjectionType;

    /** This property handles the orthographic zoom
      *
      * To access this property do: OrthoZoom.Get().
      * To set this property do: OrthoZoom.Set(fbxDouble1).
      *
      * Default value is 1.0.
      */
    KFbxTypedProperty<fbxDouble1>                       OrthoZoom;

    // -----------------------------------------------------------------------
    // Depth Of Field & Anti Aliasing
    // -----------------------------------------------------------------------

    /** This property handles the use real time DOF and AA flag
      *
      * To access this property do: UseRealTimeDOFAndAA.Get().
      * To set this property do: UseRealTimeDOFAndAA.Set(fbxBool1).
      *
      * Default value is false.
      */
    KFbxTypedProperty<fbxBool1>                         UseRealTimeDOFAndAA;

    /** This property handles the use depth of field flag
      *
      * To access this property do: UseDepthOfField.Get().
      * To set this property do: UseDepthOfField.Set(fbxBool1).
      *
      * Default value is false
      */
    KFbxTypedProperty<fbxBool1>                         UseDepthOfField;

    /** This property handles the focus source
      *
      * To access this property do: FocusSource.Get().
      * To set this property do: FocusSource.Set(ECameraFocusDistanceSource).
      *
      * Default value is eCAMERA_INTEREST
      */
    KFbxTypedProperty<ECameraFocusDistanceSource>       FocusSource;

    /** This property handles the focus angle (in degrees)
      *
      * To access this property do: FocusAngle.Get().
      * To set this property do: FocusAngle.Set(fbxDouble1).
      *
      * Default value is 3.5
      */
    KFbxTypedProperty<fbxDouble1>                       FocusAngle;

    /** This property handles the focus distance
      *
      * To access this property do: FocusDistance.Get().
      * To set this property do: FocusDistance.Set(fbxDouble1).
      *
      * Default value is 200.0
      */
    KFbxTypedProperty<fbxDouble1>                       FocusDistance;

    /** This property handles the use anti aliasing flag
      *
      * To access this property do: UseAntialiasing.Get().
      * To set this property do: UseAntialiasing.Set(fbxBool1).
      *
      * Default value is false
      */
    KFbxTypedProperty<fbxBool1>                         UseAntialiasing;

    /** This property handles the anti aliasing intensity
      *
      * To access this property do: AntialiasingIntensity.Get().
      * To set this property do: AntialiasingIntensity.Set(fbxDouble1).
      *
      * Default value is 0.77777
      */
    KFbxTypedProperty<fbxDouble1>                       AntialiasingIntensity;

    /** This property handles the anti aliasing method
      *
      * To access this property do: AntialiasingMethod.Get().
      * To set this property do: AntialiasingMethod.Set(ECameraAntialiasingMethod).
      *
      * Default value is eOVERSAMPLING_ANTIALIASING
      */
    KFbxTypedProperty<ECameraAntialiasingMethod>        AntialiasingMethod;

    // -----------------------------------------------------------------------
    // Accumulation Buffer
    // -----------------------------------------------------------------------

    /** This property handles the use accumulation buffer flag
      *
      * To access this property do: UseAccumulationBuffer.Get().
      * To set this property do: UseAccumulationBuffer.Set(fbxBool1).
      *
      * Default value is false
      */
    KFbxTypedProperty<fbxBool1>                         UseAccumulationBuffer;

    /** This property handles the frame sampling count
      *
      * To access this property do: FrameSamplingCount.Get().
      * To set this property do: FrameSamplingCount.Set(fbxInteger1).
      *
      * Default value is 7
      */
    KFbxTypedProperty<fbxInteger1>                      FrameSamplingCount;

    /** This property handles the frame sampling type
      *
      * To access this property do: FrameSamplingType.Get().
      * To set this property do: FrameSamplingType.Set(ECameraSamplingType).
      *
      * Default value is eSTOCHASTIC
      */
    KFbxTypedProperty<ECameraSamplingType>              FrameSamplingType;

///////////////////////////////////////////////////////////////////////////////
//
//  WARNING!
//
//  Anything beyond these lines may not be documented accurately and is
//  subject to change without notice.
//
///////////////////////////////////////////////////////////////////////////////
#ifndef DOXYGEN_SHOULD_SKIP_THIS

    friend class KFbxGlobalCameraSettings;

    virtual KFbxObject& Copy(const KFbxObject& pObject);
    virtual KFbxObject* Clone(KFbxObject* pContainer, KFbxObject::ECloneType pCloneType) const;

protected:
    KFbxCamera(KFbxSdkManager& pManager, char const* pName);

    virtual bool ConstructProperties(bool pForceSet);

    /**
      * Used to retrieve the KProperty list from an attribute
      */
    virtual KString     GetTypeName() const;
    virtual KStringList GetTypeFlags() const;

private:
    double ComputePixelRatio(kUInt pWidth, kUInt pHeight, double pScreenRatio = 1.3333333333);

    // Background Properties
    KString mBackgroundMediaName;
    KString mBackgroundFileName;

    // Foreground Properties
    KString mForegroundMediaName;
    KString mForegroundFileName;

    friend class KFbxNode;

#endif // #ifndef DOXYGEN_SHOULD_SKIP_THIS

};

#define ImageFit	

typedef KFbxCamera* HKFbxCamera;

inline EFbxType FbxTypeOf( KFbxCamera::ECameraAntialiasingMethod const &pItem )         { return eENUM; }
inline EFbxType FbxTypeOf( KFbxCamera::ECameraApertureFormat const &pItem )             { return eENUM; }
inline EFbxType FbxTypeOf( KFbxCamera::ECameraApertureMode const &pItem )               { return eENUM; }
inline EFbxType FbxTypeOf( KFbxCamera::ECameraAspectRatioMode const &pItem )            { return eENUM; }
inline EFbxType FbxTypeOf( KFbxCamera::ECameraFrontBackPlaneDisplayMode const &pItem )  { return eENUM; }
inline EFbxType FbxTypeOf( KFbxCamera::ECameraFrontBackPlaneDistanceMode const &pItem )	{ return eENUM; }
inline EFbxType FbxTypeOf( KFbxCamera::ECameraPlateDrawingMode const &pItem )			{ return eENUM; }
inline EFbxType FbxTypeOf( KFbxCamera::ECameraFocusDistanceSource const &pItem )        { return eENUM; }
inline EFbxType FbxTypeOf( KFbxCamera::ECameraFormat const &pItem )                     { return eENUM; }
inline EFbxType FbxTypeOf( KFbxCamera::ECameraGateFit const &pItem )                    { return eENUM; }
inline EFbxType FbxTypeOf( KFbxCamera::ECameraProjectionType const &pItem )             { return eENUM; }
inline EFbxType FbxTypeOf( KFbxCamera::ECameraRenderOptionsUsageTime const &pItem )     { return eENUM; }
inline EFbxType FbxTypeOf( KFbxCamera::ECameraSafeAreaStyle const &pItem )              { return eENUM; }
inline EFbxType FbxTypeOf( KFbxCamera::ECameraSamplingType const &pItem )               { return eENUM; }
inline EFbxType FbxTypeOf( KFbxCamera::ECameraFilmRollOrder const &pItem )               { return eENUM; }

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXCAMERA_H

