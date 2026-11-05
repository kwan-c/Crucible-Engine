/*************************************************************************
* \file:         GraphAssets.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The starter contents of a new .behaviorgraph and a new
*                .stategraph - what Asset Browser > Create writes.
*
* \details
*                Built as real graphs and written by the engine's own
*                encoders, so a new file is always one the loaders accept.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <string>

namespace Crucible::GraphAssets {

    // A behaviour graph with just its On Start root: ready to add nodes under.
    std::string NewBehaviorGraphJson();

    // A state graph with one empty "Idle" state, which is also the entry state.
    std::string NewStateGraphJson();

}   // NAMESPACE CRUCIBLE::GRAPHASSETS
