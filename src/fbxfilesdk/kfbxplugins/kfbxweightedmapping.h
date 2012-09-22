/*!  \file kfbxweightedmapping.h
 */

#ifndef FBXFILESDK_KFBXPLUGINS_KFBXWEIGHTEDMAPPING_H
#define FBXFILESDK_KFBXPLUGINS_KFBXWEIGHTEDMAPPING_H

/**************************************************************************************

 Copyright (C) 2001 - 2009 Autodesk, Inc. and/or its licensors.
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

#include <fbxfilesdk/components/kbaselib/klib/karrayul.h>

#include <fbxfilesdk/fbxfilesdk_nsbegin.h>

/**	FBX SDK weighted mapping class.
  * \nosubgrouping
  */
class KFBX_DLL KFbxWeightedMapping
{

public:

	typedef enum 
	{
		eSOURCE,
		eDESTINATION
	} ESet;

	struct KElement
	{
		int mIndex;
		double mWeight;
	};

	/** 
	  * \name Constructor and Destructor
	  */
	//@{

	/** Constructor
	  * \param pSourceSize       Source set size
	  * \param pDestinationSize  Destination set size
	  */
	KFbxWeightedMapping(int pSourceSize, int pDestinationSize);

	//! Destructor
	~KFbxWeightedMapping();
    //@}


	/** Remove all weighted relations and give new source and destination sets sizes.
	  * \param pSourceSize       New source set size
	  * \param pDestinationSize  New destination set size
	  */
	void Reset(int pSourceSize, int pDestinationSize);

	/** Add a weighted relation.
	  * \param pSourceIndex      
	  * \param pDestinationIndex 
	  * \param pWeight           
	  */
	void Add(int pSourceIndex, int pDestinationIndex, double pWeight);

	/** Get the number of elements of a set.
	  * \param pSet              
	  */
	int GetElementCount(ESet pSet) const;

	/** Get the number of relations an element of a set is linked to.
	  * \param pSet               
	  * \param pElement          
	  */
	int GetRelationCount(ESet pSet, int pElement) const;

	/** Get one of the relations an element of a set is linked to.
	  * \param pSet              
	  * \param pElement          
	  * \param pIndex            
	  * \return                  KElement gives the index of an element in the other set and the assigned weight.
	  */
	KElement& GetRelation(ESet pSet, int pElement, int pIndex);

	/** Given the index of an element in the other set, get the index of one of the relations 
	  *  an element of a set is linked to. Returns -1 if there is not relation between these elements.
	  * \param pSet
	  * \param pElementInSet
	  * \param pElementInOtherSet
	  * \return                  the index of one of the relations, -1 if there is not relation between these elements.         
	  */
	int GetRelationIndex(ESet pSet, int pElementInSet, int pElementInOtherSet) const;

	/** Get the sum of the weights from the relations an element of a set is linked to.
	  * \param pSet
	  * \param pElement
	  * \param pAbsoluteValue
	  * \return                 the sum of the weights  from the relations.
	  */
	double GetRelationSum(ESet pSet, int pElement, bool pAbsoluteValue) const;
	

	/** Normalize the weights of the relations of all the elements of a set.
	  * \param pSet
	  * \param pAbsoluteValue
	  */
	void Normalize(ESet pSet, bool pAbsoluteValue);
	
private:

	//! Remove all weighted relations.
	void Clear();

	KArrayTemplate<KArrayTemplate<KElement>*> mElements[2];

};		

typedef class KFBX_DLL KArrayTemplate<KFbxWeightedMapping::KElement> KArrayTemplateKElement;
typedef class KFBX_DLL KArrayTemplate<KArrayTemplate<KFbxWeightedMapping::KElement>*> KArrayTemplateKArrayTemplateKElement;
typedef KFbxWeightedMapping* HKFbxWeightedMapping;

#include <fbxfilesdk/fbxfilesdk_nsend.h>

#endif // FBXFILESDK_KFBXPLUGINS_KFBXWEIGHTEDMAPPING_H

