#pragma once
#include "SkeletalModel.h"
#include <ufbx.h>
#include <set>
#include <algorithm>

namespace MipsyncEngine {
struct SkeletalPartition {
    std::vector<uint32_t> indices;
    std::vector<SkeletalModelSubmesh> submeshes;
    std::vector<SkeletalModelMeshPart> parts;
};
// A triangle belongs to its strongest bone influence (sum across its three
// vertices). Descendant roots select that region without changing skin weights,
// skeleton, clips, original geometry or whole-model display fitting.
inline SkeletalPartition PartitionSkeletalModel(const SkeletalModelAsset& model,
        const std::vector<std::string>& roots,const std::vector<int>& selectedParts) {
    std::set<const ufbx_node*> rootNodes;
    for(const auto& bone:model.bones)if(std::find(roots.begin(),roots.end(),bone.name)!=roots.end())rootNodes.insert(bone.node);
    std::vector<bool> selected(model.bones.size(),roots.empty());
    for(size_t i=0;i<model.bones.size();++i)for(auto* node=model.bones[i].node;node;node=node->parent)
        if(rootNodes.contains(node)){selected[i]=true;break;}
    SkeletalPartition result;
    for(size_t partIndex=0;partIndex<model.meshParts.size();++partIndex) {
        if(!selectedParts.empty()&&std::find(selectedParts.begin(),selectedParts.end(),static_cast<int>(partIndex))==selectedParts.end())continue;
        auto part=model.meshParts[partIndex];const uint32_t start=static_cast<uint32_t>(result.indices.size());
        for(uint32_t offset=part.indexOffset;offset+2<part.indexOffset+part.indexCount&&offset+2<model.sourceIndices.size();offset+=3) {
            if(!roots.empty()) {
                std::vector<float> weights(model.bones.size(),0.f);
                for(int corner=0;corner<3;++corner) {
                    const auto& v=model.sourceVertices.at(model.sourceIndices[offset+corner]);
                    for(int influence=0;influence<4;++influence) {
                        const int bone=static_cast<int>(v.boneIndices[influence]+.5f);
                        if(bone>=0&&bone<static_cast<int>(weights.size()))weights[bone]+=v.boneWeights[influence];
                    }
                }
                if(weights.empty())continue;
                const auto dominant=static_cast<size_t>(std::max_element(weights.begin(),weights.end())-weights.begin());
                if(weights[dominant]<=0||!selected[dominant])continue;
            }
            uint32_t material=part.defaultMaterialIndex;
            for(const auto& sub:model.submeshes)if(offset>=sub.indexOffset&&offset<sub.indexOffset+sub.indexCount){material=sub.materialIndex;break;}
            if(result.submeshes.empty()||result.submeshes.back().materialIndex!=material||result.submeshes.back().indexOffset<start)
                result.submeshes.push_back({static_cast<uint32_t>(result.indices.size()),0,material});
            result.submeshes.back().indexCount+=3;
            result.indices.insert(result.indices.end(),model.sourceIndices.begin()+offset,model.sourceIndices.begin()+offset+3);
        }
        part.indexOffset=start;part.indexCount=static_cast<uint32_t>(result.indices.size())-start;
        if(part.indexCount)result.parts.push_back(part);
    }
    return result;
}
}
