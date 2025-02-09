#include "geomalembicreader.h"

using namespace VR;

int isValidMappingChannel(const MeshChannel &chan) {
	if (chan.channelID<VERT_TEX_CHANNEL0 || chan.channelID>=VERT_TEX_TOPO_CHANNEL0)
		return false;
	return true;
}

struct MeshVoxelGuardRAII {
	MeshVoxel *voxel;
	MeshFile *meshFile;

	MeshVoxelGuardRAII(MeshFile &mfile, MeshVoxel *meshVoxel):meshFile(&mfile), voxel(meshVoxel) {}
	~MeshVoxelGuardRAII(void) {
		if (voxel)
			meshFile->releaseVoxel(voxel);
		voxel=NULL;
	}

	void reassign(MeshVoxel *newVoxel) {
		if (voxel)
			meshFile->releaseVoxel(voxel);

		voxel=newVoxel;
	}
};

#if 0
AlembicMeshSource* GeomAlembicReader::createGeomStaticMesh(
	VRayRenderer *vray,
	MeshFile &abcFile,
	int voxelIndex,
	int createInstance,
	DefaultMeshSetsData &meshSets,
	int nsamples,
	double frameStart,
	double frameEnd,
	double frameTime
) {
	MeshVoxel *voxel=abcFile.getVoxel(voxelIndex, nsamples<<16, NULL, NULL);
	if (!voxel)
		return NULL;

	MeshVoxelGuardRAII voxelRAII(abcFile, voxel);

	// First figure out the name of the Alembic object from the face IDs in the voxel.
	// For Alembic files, all faces have the same face ID and we can use it to read the
	// name of the shader set, which is the name of the Alembic object.
	int mtlID=0;
	const MeshChannel *faceInfoChannel=voxel->getChannel(FACE_INFO_CHANNEL);
	if (faceInfoChannel) {
		const FaceInfoData *faceInfo=static_cast<FaceInfoData*>(faceInfoChannel->data);
		if (faceInfo)
			mtlID=faceInfo[0].mtlID;
	}

	// The Alembic name is stored as the shader set name.
	tchar meshPluginName[512]="";
	StringID strID=abcFile.getShaderSetStringID(voxel, mtlID);
	if (strID.id!=0) {
		strID=vray->getStringManager()->getStringID(strID.id);
		vutils_sprintf_n(meshPluginName, COUNT_OF(meshPluginName), "voxel_%s", strID.str.ptr());
	} else {
		vutils_sprintf_n(meshPluginName, COUNT_OF(meshPluginName), "voxel_%i", meshSources.count());
	}

	VRayPlugin *meshPlugin=newPlugin("GeomMeshFile", meshPluginName);
	if (!meshPlugin)
		return nullptr;

	TransformsList vertexTransforms;
	vertexTransforms.setCount(nsamples);

	TimesList times;
	times.setCount(nsamples);

	AlembicMeshSource *abcMeshSource=new AlembicMeshSource;
	abcMeshSource->geomStaticMesh=meshPlugin;

	// true if we want to read velocity information and false to just sample positions.
	// Note that the Alembic reader inside the MeshFile implementation may still internally use
	// velocity information from the Alembic file to interpolate positions.
	int useVelocity=true;

	meshPlugin->setParameter(&abcMeshSource->dynamicGeometryParam);
	meshPlugin->setParameter(&abcMeshSource->verticesParam);
	meshPlugin->setParameter(&abcMeshSource->facesParam);
	meshPlugin->setParameter(&abcMeshSource->mapChannelsParam);
	meshPlugin->setParameter(&abcMeshSource->normalsParam);
	meshPlugin->setParameter(&abcMeshSource->faceNormalsParam);
	meshPlugin->setParameter(&abcMeshSource->mapChannelNamesParam);
	if (useVelocity) {
		meshPlugin->setParameter(&abcMeshSource->velocitiesParam);
	}

	abcMeshSource->setNumTimeSteps(nsamples);

	for (int i=0; i<nsamples; i++) {
		double time=(nsamples>1)? (frameStart+(frameEnd-frameStart)*i/double(nsamples-1)) : frameTime;
		vertexTransforms[i].makeIdentity();
		times[i]=time;

		if (i>0) {
			int timeFlags=i|(nsamples<<16);
			voxel=abcFile.getVoxel(voxelIndex, timeFlags, NULL, NULL);
			voxelRAII.reassign(voxel);
		}

		if (!voxel)
			continue;

		// Set the transformation matrix
		voxel->getTM(vertexTransforms[i]);

		// Read the vertices and set them into the verticesParam
		const MeshChannel *vertsChannel=voxel->getChannel(VERT_GEOM_CHANNEL);
		const VertGeomData *verts=static_cast<VertGeomData*>(vertsChannel->data);
		int numVerts=vertsChannel->numElements;
		VR::VectorList paramVerts(numVerts);
		for (int i=0; i<numVerts; i++) {
			paramVerts[i]=verts[i];
		}
		abcMeshSource->verticesParam.addKeyframe(time, paramVerts);

		// Read the faces and set them into the facesParam
		const MeshChannel *facesChannel=voxel->getChannel(FACE_TOPO_CHANNEL);
		const FaceTopoData *faces=static_cast<FaceTopoData*>(facesChannel->data);
		int numFaces=facesChannel->numElements;
		VR::IntList paramFaces(numFaces*3);
		for (int i=0; i<numFaces; i++) {
			const FaceTopoData &face=faces[i];

			int idx=i*3;
			paramFaces[idx+0]=face.v[0];
			paramFaces[idx+1]=face.v[1];
			paramFaces[idx+2]=face.v[2];
		}
		abcMeshSource->facesParam.addKeyframe(time, paramFaces);

		// Read the normals and set them into the normalsParam and faceNormalsParam
		const MeshChannel *normalsChannel=voxel->getChannel(VERT_NORMAL_CHANNEL);
		if (normalsChannel) {
			const VertGeomData *normals=static_cast<VertGeomData*>(normalsChannel->data);
			int numNormals=normalsChannel->numElements;
			VR::VectorList paramNormals(numNormals);
			for (int i=0; i<numNormals; i++) {
				paramNormals[i]=normals[i];
			}
			abcMeshSource->normalsParam.addKeyframe(time, paramNormals);
		}

		const MeshChannel *faceNormalsChannel=voxel->getChannel(VERT_NORMAL_TOPO_CHANNEL);
		if (faceNormalsChannel) {
			const FaceTopoData *faceNormals=static_cast<FaceTopoData*>(faceNormalsChannel->data);
			int numFaceNormals=faceNormalsChannel->numElements;
			VR::IntList paramFaceNormals(numFaceNormals*3);
			for (int i=0; i<numFaceNormals; i++) {
				const FaceTopoData &face=faceNormals[i];

				int idx=i*3;
				paramFaceNormals[idx+0]=face.v[0];
				paramFaceNormals[idx+1]=face.v[1];
				paramFaceNormals[idx+2]=face.v[2];
			}
			abcMeshSource->faceNormalsParam.addKeyframe(time, paramFaceNormals);
		}

		// Read the UV/color sets
		int numMapChannels=0;
		for (int i=0; i<voxel->numChannels; i++) {
			const MeshChannel &chan=voxel->channels[i];
			if (isValidMappingChannel(chan))
				numMapChannels++;
		}

		if (numMapChannels>0) {
			AbcMapChannelsList &mapChannelsList=abcMeshSource->mapChannelsParam.addKeyframe(time);
			mapChannelsList.setCount(numMapChannels);

			int idx=0;
			for (int chanIdx=0; chanIdx<voxel->numChannels; chanIdx++) {
				const MeshChannel &chan=voxel->channels[chanIdx];
				if (isValidMappingChannel(chan)) {
					AbcMapChannel &mapChannel=mapChannelsList[idx];

					mapChannel.idx=chan.channelID-VERT_TEX_CHANNEL0;

					mapChannel.verts.setCount(chan.numElements);
					const VertGeomData *uvw=static_cast<const VertGeomData*>(chan.data);
					int numUVWs=chan.numElements;
					for (int j=0; j<numUVWs; j++) {
						mapChannel.verts[j]=uvw[j];
					}

					const MeshChannel *topoChan=voxel->getChannel(chan.depChannelID);
					if (topoChan) {
						const FaceTopoData *uvwFaces=static_cast<FaceTopoData*>(topoChan->data);
						int numUVWFaces=topoChan->numElements;

						mapChannel.faces.setCount(numUVWFaces*3);
						for (int j=0; j<numUVWFaces; j++) {
							const FaceTopoData &face=uvwFaces[j];
							int idx=j*3;
							mapChannel.faces[idx+0]=face.v[0];
							mapChannel.faces[idx+1]=face.v[1];
							mapChannel.faces[idx+2]=face.v[2];
						}
					}

					idx++;
				}
			}

			// Fill in the mapping channel names
			StringList &mapChannelNames=abcMeshSource->mapChannelNamesParam.addKeyframe(time);

			mapChannelNames.setCount(numMapChannels);
			int numUVSets=meshSets.getNumSets(MeshSetsData::meshSetType_uvSet);
			for (int i=0; i<mapChannelsList.count(); i++) {
				const tchar *setName=NULL;
				if (i<numUVSets) setName=meshSets.getSetName(MeshSetsData::meshSetType_uvSet, i);
				else setName=meshSets.getSetName(MeshSetsData::meshSetType_colorSet, i-numUVSets);

				if (NULL==setName) setName="";
				mapChannelNames[i]=setName;
			}
		}

		// If motion blur is enabled, read the vertex velocities and set them into the velocitiesParam
		if (useVelocity && vray->getSequenceData().params.moblur.on) {
			const MeshChannel *velocitiesChannel=voxel->getChannel(VERT_VELOCITY_CHANNEL);
			if (velocitiesChannel && velocitiesChannel->data && velocitiesChannel->numElements==numVerts) {
				const VertGeomData *velocities=static_cast<VertGeomData*>(velocitiesChannel->data);
				VR::VectorList velParam(numVerts);
				for (int i=0; i<numVerts; i++) {
					velParam[i]=velocities[i];
				}
				abcMeshSource->velocitiesParam.addKeyframe(time, velParam);
			}
		}
	}

	// Check if the object should have displacement/subdivision
	DisplacementSubdivParams displSubdivParams;
	getDisplacementSubdivParams(strID.str, displSubdivParams);

	VRayPlugin *displSubdivPlugin=nullptr;

	// Create displacement/subvision plugin as needed and set specific parameters.
	if (displSubdivParams.hasSubdivision) {
		// Subdivision
		vutils_strcat_n(meshPluginName, "@subdiv", COUNT_OF(meshPluginName));
		displSubdivPlugin=newPlugin("GeomStaticSmoothedMesh", meshPluginName);
		if (displSubdivPlugin) {
			displSubdivPlugin->setParameter(&abcMeshSource->preTesselateSubdivParam);
		}
	} else if (displSubdivParams.displacementTex) {
		// Only displacement
		vutils_strcat_n(meshPluginName, "@displ", COUNT_OF(meshPluginName));
		displSubdivPlugin=newPlugin("GeomDisplacedMesh", meshPluginName);
		if (displSubdivPlugin) {
			displSubdivPlugin->setParameter(&abcMeshSource->preTesselateDisplParam);
		}
	}

	// Set the general displacement/subdivision parameters as needed.
	if (displSubdivPlugin) {
		// Set the source mesh plugin.
		abcMeshSource->displSubdivSourceMeshParam.setUserObject(meshPlugin, 0 /* index */, 0.0f /* time */);
		displSubdivPlugin->setParameter(&abcMeshSource->displSubdivSourceMeshParam);

		// Set other parameters.
		displSubdivPlugin->setParameter(&abcMeshSource->displSubdivEdgeLengthParam);
		displSubdivPlugin->setParameter(&abcMeshSource->useGlobalsParam);
		displSubdivPlugin->setParameter(&abcMeshSource->maxSubdivLevelsParam);

		// Set the displacement texture, if any.
		if (displSubdivParams.displacementTex) {
			abcMeshSource->displTextureParam.setUserObject(displSubdivParams.displacementTex, 0 /* index */, 0.0f /* time */);
			displSubdivPlugin->setParameter(&abcMeshSource->displTextureParam);

			abcMeshSource->displAmountParam.setFloat(displSubdivParams.displacementAmount, 0 /* index */, 0.0f /* time */);
			displSubdivPlugin->setParameter(&abcMeshSource->displAmountParam);
		}
	}

	abcMeshSource->displSubdivPlugin=displSubdivPlugin;

	if (createInstance) {
		AlembicMeshInstance *abcMeshInstance=new AlembicMeshInstance;

		abcMeshInstance->meshIndex=meshInstances.count();
		abcMeshInstance->meshSource=abcMeshSource;
		abcMeshInstance->tms.copy(vertexTransforms);
		abcMeshInstance->times.copy(times);
		abcMeshInstance->abcName=strID.str;

		meshInstances+=abcMeshInstance;
	}

	return abcMeshSource;
}
#endif

AlembicMeshSource* GeomAlembicReader::createGeomStaticMesh(
	VRayRenderer *vray,
	MeshFile &abcFile,
	int voxelIndex,
	int createInstance,
	DefaultMeshSetsData &meshSets,
	int nsamples,
	double frameStart,
	double frameEnd,
	double frameTime
) {
	MeshVoxel *voxel=abcFile.getVoxel(voxelIndex, nsamples<<16, NULL, NULL);
	if (!voxel) {
		vray->getSequenceData().progress->error("Could not load voxel %i of file %s", voxelIndex, fileName.ptr());
		return NULL;
	}

	MeshVoxelGuardRAII voxelRAII(abcFile, voxel);

	// First figure out the name of the Alembic object from the face IDs in the voxel.
	// For Alembic files, all faces have the same face ID and we can use it to read the
	// name of the shader set, which is the name of the Alembic object.
	int mtlID=0;
	const MeshChannel *faceInfoChannel=voxel->getChannel(FACE_INFO_CHANNEL);
	if (faceInfoChannel) {
		const FaceInfoData *faceInfo=static_cast<FaceInfoData*>(faceInfoChannel->data);
		if (faceInfo)
			mtlID=faceInfo[0].mtlID;
	}

	// The Alembic name is stored as the shader set name.
	tchar meshPluginName[512]="";
	StringID strID=abcFile.getShaderSetStringID(voxel, mtlID);
	if (strID.id!=0) {
		strID=vray->getStringManager()->getStringID(strID.id);
		vutils_sprintf_n(meshPluginName, COUNT_OF(meshPluginName), "voxel_%s", strID.str.ptr());
	} else {
		vutils_sprintf_n(meshPluginName, COUNT_OF(meshPluginName), "voxel_%i", meshSources.count());
	}

	VRayPlugin *meshPlugin=newPlugin("GeomMeshFile", meshPluginName);
	if (!meshPlugin) {
		vray->getSequenceData().progress->error("Failed to create GeomMeshFile");
		return nullptr;
	}

	TransformsList vertexTransforms;
	vertexTransforms.setCount(nsamples);

	TimesList times;
	times.setCount(nsamples);

	AlembicMeshSource *abcMeshSource=new AlembicMeshSource(fileName.ptr(), strID.str.ptr());
	abcMeshSource->geomStaticMesh=meshPlugin;

	meshPlugin->setParameter(&abcMeshSource->fileNameParam);
	meshPlugin->setParameter(&abcMeshSource->objectPathParam);
	meshPlugin->setParameter(&abcMeshSource->useFullNamesParam);

	abcMeshSource->setNumTimeSteps(nsamples);

	// Check if the object should have displacement/subdivision
	DisplacementSubdivParams displSubdivParams;
	getDisplacementSubdivParams(strID.str, displSubdivParams);

	VRayPlugin *displSubdivPlugin=nullptr;

	// Create displacement/subvision plugin as needed and set specific parameters.
	if (displSubdivParams.hasSubdivision) {
		// Subdivision
		vutils_strcat_n(meshPluginName, "@subdiv", COUNT_OF(meshPluginName));
		displSubdivPlugin=newPlugin("GeomStaticSmoothedMesh", meshPluginName);
		if (displSubdivPlugin) {
			displSubdivPlugin->setParameter(&abcMeshSource->preTesselateSubdivParam);
		}
	} else if (displSubdivParams.displacementTex) {
		// Only displacement
		vutils_strcat_n(meshPluginName, "@displ", COUNT_OF(meshPluginName));
		displSubdivPlugin=newPlugin("GeomDisplacedMesh", meshPluginName);
		if (displSubdivPlugin) {
			displSubdivPlugin->setParameter(&abcMeshSource->preTesselateDisplParam);
		}
	}

	// Set the general displacement/subdivision parameters as needed.
	if (displSubdivPlugin) {
		// Set the source mesh plugin.
		abcMeshSource->displSubdivSourceMeshParam.setUserObject(meshPlugin, 0 /* index */, 0.0f /* time */);
		displSubdivPlugin->setParameter(&abcMeshSource->displSubdivSourceMeshParam);

		// Set other parameters.
		displSubdivPlugin->setParameter(&abcMeshSource->displSubdivEdgeLengthParam);
		displSubdivPlugin->setParameter(&abcMeshSource->useGlobalsParam);
		displSubdivPlugin->setParameter(&abcMeshSource->maxSubdivLevelsParam);

		// Set the displacement texture, if any.
		if (displSubdivParams.displacementTex) {
			abcMeshSource->displTextureParam.setUserObject(displSubdivParams.displacementTex, 0 /* index */, 0.0f /* time */);
			displSubdivPlugin->setParameter(&abcMeshSource->displTextureParam);

			abcMeshSource->displAmountParam.setFloat(displSubdivParams.displacementAmount, 0 /* index */, 0.0f /* time */);
			displSubdivPlugin->setParameter(&abcMeshSource->displAmountParam);
		}
	}

	abcMeshSource->displSubdivPlugin=displSubdivPlugin;

	if (createInstance) {
		AlembicMeshInstance *abcMeshInstance=new AlembicMeshInstance;

		abcMeshInstance->meshIndex=meshInstances.count();
		abcMeshInstance->meshSource=abcMeshSource;
		abcMeshInstance->tms.copy(vertexTransforms);
		abcMeshInstance->times.copy(times);
		abcMeshInstance->abcName=strID.str;

		meshInstances+=abcMeshInstance;
	}

	return abcMeshSource;
}

