#pragma once

#include "openMVG/sfm/pipelines/hierarchical_hyper/submap.hpp"
#include <string>

namespace openMVG{
namespace sfm{

enum eSubmapDiagnostic
{
  OK,
  NO_RECONSTRUCTION,
  BAD_RECONSTRUCTION,
  NO_RECONSTRUCTED_SEPARATORS,
  NO_COMMON_RECONSTRUCTED_SEPARATORS
};

bool SaveHsfmSubmap(
    const HsfmSubmap & submap,
    const std::string & filename);

bool LoadHsfmSubmap(
    HsfmSubmap & submap,
    const std::string & filename);

bool SaveSubmaps(const HsfmSubmaps &submaps,
    const std::string & filename);

bool LoadSubmaps(
    HsfmSubmaps & submaps,
    const std::string & filename);

IndexT getSiblingSubmapId(const HsfmSubmaps &submaps, const IndexT submap_id);

// Check submaps "health"
std::map<IndexT, eSubmapDiagnostic> CheckSubmaps(const HsfmSubmaps & submaps);

}
}