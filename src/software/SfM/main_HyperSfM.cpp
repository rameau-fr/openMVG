#include "openMVG/cameras/Camera_Common.hpp"
#include "openMVG/cameras/Cameras_Common_command_line_helper.hpp"
#include "openMVG/sfm/pipelines/sfm_features_provider.hpp"
#include "openMVG/sfm/pipelines/sfm_matches_provider.hpp"
#include "openMVG/sfm/sfm_data.hpp"
#include "openMVG/sfm/sfm_data_io.hpp"
#include "openMVG/sfm/sfm_view.hpp"
#include "openMVG/system/timer.hpp"

#include "openMVG/sfm/pipelines/hierarchical_hyper/hypercluster.hpp"
#include "openMVG/sfm/pipelines/hierarchical_hyper/submap_SfM.hpp"
#include "openMVG/sfm/pipelines/hierarchical_hyper/submap_merger.hpp"
#include "openMVG/sfm/pipelines/hierarchical_hyper/submap_utilities.hpp"

#include "third_party/cmdLine/cmdLine.h"
#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"

using namespace openMVG;
using namespace openMVG::cameras;
using namespace openMVG::sfm;

std::unique_ptr<SubmapThresholdChecker> getThresholdChecker(const int tracks_threshold, const int view_threshold)
{
  if (tracks_threshold < 0)
  {
    std::cout << "View threshold chosen : " << view_threshold << std::endl;
    return std::unique_ptr<SubmapThresholdChecker>(new SubmapViewThresholdChecker(view_threshold));
  }
  else
  {
    std::cout << "Tracks threshold chosen : " << tracks_threshold << std::endl;
    return std::unique_ptr<SubmapThresholdChecker>(new SubmapTracksThresholdChecker(tracks_threshold));
  }
}

int main(int argc, char **argv)
{
  CmdLine cmd;

  std::string sSfM_Data_Filename;
  std::string sMatchesDir;
  std::string sOutDir = "";
  int view_threshold_clustering = -1;
  int tracks_threshold_clustering = -1;
  int i_User_camera_model = PINHOLE_CAMERA_RADIAL3;

  cmd.add( make_option('i', sSfM_Data_Filename, "input_file") );
  cmd.add( make_option('m', sMatchesDir, "matchdir") );
  cmd.add( make_option('t', tracks_threshold_clustering, "tracks_threshold") );
  cmd.add( make_option('v', view_threshold_clustering, "views_threshold") );
  cmd.add( make_option('o', sOutDir, "outdir") );
  cmd.add( make_option('c', i_User_camera_model, "camera_model") );

  try {
    if (argc == 1) throw std::string("Invalid parameter.");
    cmd.process(argc, argv);
  } catch (const std::string& s) {
    std::cerr << "Usage: " << argv[0] << '\n'
    << "[-i|--input_file] path to a SfM_Data scene\n"
    << "[-m|--matchdir] path to the matches that corresponds to the provided SfM_Data scene\n"
    << "[-o|--outdir] path where the output data will be stored\n"
    << "[-v|--views_threshold] clustering threshold in number of views per submap\n"
    << "[-t|--tracks_threshold] clustering threshold in number of tracks per submap\n"
    << "NOTE : if user selects both a track_threshold and a view_threshold, the tracks threshold gets priority !\n"
    << "[-c|--camera_model] Camera model type for view with unknown intrinsic:\n"
      << "\t 1: Pinhole \n"
      << "\t 2: Pinhole radial 1\n"
      << "\t 3: Pinhole radial 3 (default)\n"
      << "\t 4: Pinhole radial 3 + tangential 2\n"
      << "\t 5: Pinhole fisheye\n";
  }

  // verify threshold input
  if (tracks_threshold_clustering < 0 && view_threshold_clustering < 0)
  {
    std::cerr << std::endl
      << "Could not find any valid input threshold value ! Threshold must be positive !" << std::endl
      << "Views threshold : " << view_threshold_clustering << std::endl
      << "tracks threshold : " << tracks_threshold_clustering << std::endl
      << "add a tracks threshold with -t, a view threshold with -v" << std::endl;
    return EXIT_FAILURE;
  }


  // Load input SfM_Data scene
  SfM_Data sfm_data;
  if (!Load(sfm_data, sSfM_Data_Filename, ESfM_Data(VIEWS|INTRINSICS))) {
    std::cerr << std::endl
      << "The input SfM_Data file \""<< sSfM_Data_Filename << "\" cannot be read." << std::endl;
    return EXIT_FAILURE;
  }

  // Init the regions_type from the image describer file (used for image regions extraction)
  using namespace openMVG::features;
  const std::string sImage_describer = stlplus::create_filespec(sMatchesDir, "image_describer", "json");
  std::unique_ptr<Regions> regions_type = Init_region_type_from_file(sImage_describer);
  if (!regions_type)
  {
    std::cerr << "Invalid: "
      << sImage_describer << " regions type file." << std::endl;
    return EXIT_FAILURE;
  }

  // Features reading
  std::shared_ptr<Features_Provider> feats_provider = std::make_shared<Features_Provider>();
  if (!feats_provider->load(sfm_data, sMatchesDir, regions_type)) {
    std::cerr << std::endl
      << "Invalid features." << std::endl;
    return EXIT_FAILURE;
  }

  // Matches reading
  std::shared_ptr<Matches_Provider> matches_provider = std::make_shared<Matches_Provider>();

  // Try to read the two matches file formats
  if
  (
    !(matches_provider->load(sfm_data, stlplus::create_filespec(sMatchesDir, "matches.f.txt")) ||
      matches_provider->load(sfm_data, stlplus::create_filespec(sMatchesDir, "matches.f.bin")))
  )
  {
    std::cerr << std::endl
      << "Invalid matches file." << std::endl;
    return EXIT_FAILURE;
  }

  if (sOutDir.empty())  {
    std::cerr << "\nIt is an invalid output directory" << std::endl;
    return EXIT_FAILURE;
  }

  if (!stlplus::folder_exists(sOutDir))
  {
    if (!stlplus::folder_create(sOutDir))
    {
      std::cerr << "\nCannot create the output directory" << std::endl;
    }
  }

  openMVG::system::Timer timer;

  // create tracks
  tracks::STLMAPTracks map_tracks;
  // Compute tracks from matches
  tracks::TracksBuilder tracksBuilder;
  // List of features matches for each couple of images
  const openMVG::matching::PairWiseMatches & map_Matches = matches_provider->pairWise_matches_;
  std::cout << "\n" << "...Track building" << std::endl;

  tracksBuilder.Build(map_Matches);
  std::cout << "\n" << "...Track filtering" << std::endl;
  tracksBuilder.Filter();
  std::cout << "\n" << "...Track export to internal struct" << std::endl;

  //-- Build tracks with STL compliant type :
  tracksBuilder.ExportToSTL(map_tracks);

  // choose which threshold to use
  std::unique_ptr<SubmapThresholdChecker> threshold_checker = getThresholdChecker(tracks_threshold_clustering, view_threshold_clustering);

  // cluster the scene into submaps
  std::cout << "...Start Clustering : "<< timer << std::endl;
  HyperCluster clusterer(sfm_data, map_tracks, std::move(threshold_checker));
  clusterer.recursivePartitioning();
  clusterer.exportTreeGraph(stlplus::create_filespec(sOutDir, "hyperCluster"));
  HsfmSubmaps submaps = clusterer.getSubMaps();
  std::cout << "Clustering Done : " << timer << std::endl;

  // reconstruct each leaf submap separately
  std::cout << "...Start Reconstruction of Leaf Submaps : " << timer << std::endl;
  for (auto & smap : submaps)
  {
    if (smap.second.is_parent)
      continue;

    const IndexT & submap_id = smap.first;
    HsfmSubmap & submap = smap.second;

    SubmapSfMReconstructionEngine sfmEngine(submap, map_tracks, sOutDir, stlplus::create_filespec(sOutDir, "Reconstruction_Report.html"));

    // configure feature and matches provider
    sfmEngine.SetFeaturesProvider(feats_provider.get());
    sfmEngine.SetMatchesProvider(matches_provider.get());

    // Configure reconstruction parameters
    std::string sIntrinsic_refinement_options = "NONE";
    const cameras::Intrinsic_Parameter_Type intrinsic_refinement_options =
      cameras::StringTo_Intrinsic_Parameter_Type(sIntrinsic_refinement_options);
    sfmEngine.Set_Intrinsics_Refinement_Type(intrinsic_refinement_options);
    sfmEngine.SetUnknownCameraType(EINTRINSIC(i_User_camera_model));

    std::cout << "...Reconstruction of Submap nb " << submap_id << std::endl << std::endl;
    sfmEngine.Process();
    submap.sfm_data = sfmEngine.Get_SfM_Data();

    // save submap
    ExportSubmapData(submaps, submap_id,
        stlplus::create_filespec(sOutDir, "sfm_data_" + std::to_string(submap_id)));
  }

  std::cout << "...Export Submaps Before Merge" << std::endl;
  SaveSubmaps(submaps,
      stlplus::create_filespec(sOutDir, "submaps_before_merge", "json"));

  std::cout << "...Start Merging Submaps : " << timer << std::endl;
  SubmapMerger merger(submaps);
  merger.Merge();
  submaps = merger.getSubmaps();

  std::cout << "...Export Newly Merged Submaps" << std::endl;
  for (const auto & smap : submaps)
  {
    // leaf submaps have already been saved a few lines back
    if (smap.second.is_parent)
      ExportSubmapData(submaps, smap.first,
          stlplus::create_filespec(sOutDir, "sfm_data_" + std::to_string(smap.first)));
  }

  SaveSubmaps(submaps,
      stlplus::create_filespec(sOutDir, "submaps", "json"));

  // one last bundle adjustment on the root submap (with intrinsics optimization)
  Bundle_Adjustment_Ceres ba_object;

  std::cout << "...Run Final Bundle Adjustment on Merged Scene (also adjust intrinsics)" << std::endl;
  ba_object.Adjust(submaps.at(0).sfm_data,
    Optimize_Options(
      Intrinsic_Parameter_Type::ADJUST_ALL,
      Extrinsic_Parameter_Type::ADJUST_ALL,
      Structure_Parameter_Type::ADJUST_ALL));

  std::cout << "...Export SfM_Data to disk." << std::endl;
  Save(submaps.at(0).sfm_data,
    stlplus::create_filespec(sOutDir, "sfm_data", ".bin"),
    ESfM_Data(ALL));

  Save(submaps.at(0).sfm_data,
     stlplus::create_filespec(sOutDir, "cloud_and_poses", "ply"),
     ESfM_Data(ALL));

  std::cout << std::endl << " Total HyperSfM took (s): " << timer.elapsed() << std::endl;

  return EXIT_SUCCESS;
}
