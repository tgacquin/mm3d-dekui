// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mm3d/message_interpolation.h"
#include "mm3d/file_select_interpolation.h"
#include "mm3d/inventory_interpolation.h"
#include "mm3d/item_slide_interpolation.h"

namespace MM3D::CursorInterpolation {
using MessageInterpolation::Panel;
using MessageInterpolation::Snapshot;
using MessageInterpolation::Values;
inline std::mutex mutex;
inline std::vector<Snapshot> snapshots;
inline void Publish(std::uint64_t frame,std::uint32_t generation,const std::vector<Panel>& panels) {
    std::scoped_lock lock(mutex);
    std::erase_if(snapshots,[&](const auto& s){
        return s.generation!=generation||s.frame>frame||frame-s.frame>1;
    });
    for(const auto& p:panels) {
        if(std::none_of(snapshots.begin(),snapshots.end(),[&](const auto& s){
            return s.frame==frame&&s.panel==p;
        })&&snapshots.size()<512)snapshots.push_back({frame,generation,p});
    }
}
// Match each submitted quad independently; a batch can contain both a cursor
// and moving menu artwork. Float24 upload rounding is allowed, stale ownership
// and different atlas cells are not.
inline unsigned Mask(std::uint64_t frame,std::uint32_t generation,const Values& u,unsigned vertices) {
    if(!vertices||vertices%6||vertices>96)return 0;
    std::scoped_lock lock(mutex);
    unsigned mask=0;
    for(unsigned q=0;q<vertices/6;++q) {
        const auto& descriptor=u[10+4*q];
        const float uv=descriptor[2];
        if((descriptor[0]!=0&&descriptor[0]!=1)||descriptor[1]!=3||
           !std::isfinite(uv)||uv<0||uv>94||std::floor(uv)!=uv)continue;
        for(const auto& s:snapshots) {
            if(s.generation!=generation||(s.frame!=frame&&s.frame+1!=frame))continue;
            bool match=true;
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
                match &= MessageInterpolation::Near(s.panel.transform[r][c],u[11+4*q+r][c],.02f);
            for(unsigned r=0;r<2;++r)for(unsigned c=0;c<4;++c)
                match &= MessageInterpolation::Near(s.panel.uv[r][c],u[unsigned(uv)+r][c],.00001f);
            if(match){mask|=1u<<q;break;}
        }
    }
    return mask;
}
// Keep the current native spatial pose, while retaining native fades and
// interpolation of every non-cursor quad in the same batch. Applied before
// either CPU replay or GPU uniform upload.
inline void Snap(InventoryInterpolation::Pose& pose,const Values& u,unsigned mask) {
    for(unsigned q=0;q<16;++q)if(mask&(1u<<q))
        for(unsigned r=0;r<3;++r)pose.values[11+4*q+r]=u[11+4*q+r];
}
inline void Snap(FileSelectInterpolation::Pose& pose,const Values& u,unsigned mask) {
    for(unsigned q=0;q<pose.count;++q)if(mask&(1u<<q)) {
        pose.x[q]=u[11+4*q][0];pose.y[q]=u[12+4*q][0];
        if(pose.rectangles)for(unsigned r=0;r<2;++r)(*pose.rectangles)[2*q+r]=u[11+4*q+r];
    }
}
inline void Snap(ItemSlideInterpolation::Pose& pose,const Values& u,unsigned mask) {
    for(unsigned q=0;q<pose.count;++q)if(mask&(1u<<q))pose.x[q]=u[11+4*q][0];
}
}
