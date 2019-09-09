/////////////////////////////////////////////////////////////////////////////
/// Class:       CRTSimHitProducer
/// Module Type: producer
/// File:        CRTSimHitProducer_module.cc
///
/// Author:         Thomas Brooks
/// E-mail address: tbrooks@fnal.gov
///
/// Modified from CRTSimHitProducer by Thomas Warburton.
/////////////////////////////////////////////////////////////////////////////

// sbndcode includes
#include "sbndcode/CRT/CRTProducts/CRTData.hh"
#include "sbndcode/CRT/CRTProducts/CRTHit.hh"

// Framework includes
#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Event.h" 
#include "canvas/Persistency/Common/Ptr.h" 
#include "canvas/Persistency/Common/PtrVector.h" 
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/SubRun.h"
#include "art/Framework/Services/Optional/TFileService.h" 
#include "art/Framework/Services/Optional/TFileDirectory.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "canvas/Persistency/Common/FindManyP.h"

#include "canvas/Utilities/InputTag.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

#include <memory>
#include <iostream>
#include <map>
#include <iterator>
#include <algorithm>

// LArSoft
#include "lardataobj/Simulation/SimChannel.h"
#include "lardataobj/Simulation/AuxDetSimChannel.h"
#include "larcore/Geometry/Geometry.h"
#include "larcore/Geometry/AuxDetGeometry.h"
#include "larcorealg/Geometry/GeometryCore.h"
#include "larcorealg/Geometry/PlaneGeo.h"
#include "larcorealg/Geometry/WireGeo.h"
#include "lardataobj/RecoBase/OpFlash.h"
#include "lardata/Utilities/AssociationUtil.h"
#include "lardata/DetectorInfoServices/LArPropertiesService.h"
#include "lardata/DetectorInfoServices/DetectorPropertiesService.h"
#include "lardata/DetectorInfoServices/DetectorClocksService.h"
#include "lardataobj/RawData/ExternalTrigger.h"
#include "larcoreobj/SimpleTypesAndConstants/PhysicalConstants.h"
#include "larcoreobj/SimpleTypesAndConstants/geo_types.h"

// ROOT
#include "TTree.h"
#include "TFile.h"
#include "TH1D.h"
#include "TH2D.h"
#include "TVector3.h"
#include "TGeoManager.h"

namespace {
  // Local namespace for local functions

    
}

namespace sbnd {

  struct CRTStrip {
    double t0; 
    uint32_t channel; 
    double x; 
    double ex; 
    int id1; 
    int id2; 
    double pes;
    std::pair<std::string, unsigned> tagger;
  };
  
  class CRTSimHitProducer : public art::EDProducer {
  public:

    explicit CRTSimHitProducer(fhicl::ParameterSet const & p);

    // The destructor generated by the compiler is fine for classes
    // without bare pointers or other resource use.

    // Plugins should not be copied or assigned.
    CRTSimHitProducer(CRTSimHitProducer const &) = delete;
    CRTSimHitProducer(CRTSimHitProducer &&) = delete;
    CRTSimHitProducer & operator = (CRTSimHitProducer const &) = delete; 
    CRTSimHitProducer & operator = (CRTSimHitProducer &&) = delete;

    // Required functions.
    void produce(art::Event & e) override;

    // Selected optional functions.
    void beginJob() override;

    void endJob() override;

    void reconfigure(fhicl::ParameterSet const & p);

    std::vector<double> ChannelToLimits(CRTStrip strip);

    std::vector<double> CrtOverlap(std::vector<double> strip1, std::vector<double> strip2);

    std::pair<std::string,unsigned> ChannelToTagger(uint32_t channel);

    bool CheckModuleOverlap(uint32_t channel);

    crt::CRTHit FillCrtHit(std::vector<uint8_t> tfeb_id, std::map<uint8_t, std::vector<std::pair<int,float>>> tpesmap, float peshit, double time, double x, double ex, double y, double ey, double z, double ez, std::string tagger); 

  private:

    // Params got from fcl file.......
    art::InputTag fCrtModuleLabel;      ///< name of crt producer
    bool          fVerbose;             ///< print info
    double        fTimeCoincidenceLimit;///< minimum time between two overlapping hit crt strips [ticks]
    double        fQPed;                ///< Pedestal offset of SiPMs [ADC]
    double        fQSlope;              ///< Pedestal slope of SiPMs [ADC/photon]
    bool          fUseReadoutWindow;    ///< Only reconstruct hits within readout window
   
    // Other variables shared between different methods.
    geo::GeometryCore const* fGeometryService;                 ///< pointer to Geometry provider
    detinfo::DetectorProperties const* fDetectorProperties;    ///< pointer to detector properties provider
    art::ServiceHandle<geo::AuxDetGeometry> fAuxDetGeoService;
    const geo::AuxDetGeometry* fAuxDetGeo;
    const geo::AuxDetGeometryCore* fAuxDetGeoCore;

  }; // class CRTSimHitProducer


  CRTSimHitProducer::CRTSimHitProducer(fhicl::ParameterSet const & p)
  // Initialize member data here, if know don't want to reconfigure on the fly
  {
    // Call appropriate produces<>() functions here.
    produces< std::vector<crt::CRTHit> >();
    
    // Get a pointer to the geometry service provider
    fGeometryService = lar::providerFrom<geo::Geometry>();
    fDetectorProperties = lar::providerFrom<detinfo::DetectorPropertiesService>(); 
    fAuxDetGeo = &(*fAuxDetGeoService);
    fAuxDetGeoCore = fAuxDetGeo->GetProviderPtr();

    reconfigure(p);

  } // CRTSimHitProducer()

  void CRTSimHitProducer::reconfigure(fhicl::ParameterSet const & p)
  {
    fCrtModuleLabel       = (p.get<art::InputTag> ("CrtModuleLabel")); 
    fVerbose              = (p.get<bool> ("Verbose"));
    fTimeCoincidenceLimit = (p.get<double> ("TimeCoincidenceLimit"));
    fQPed                 = (p.get<double> ("QPed"));
    fQSlope               = (p.get<double> ("QSlope"));
    fUseReadoutWindow     = (p.get<bool> ("UseReadoutWindow"));
  }

  void CRTSimHitProducer::beginJob()
  {
    if(fVerbose){std::cout<<"----------------- CRT Hit Reco Module -------------------"<<std::endl;}
  } // beginJob()

  void CRTSimHitProducer::produce(art::Event & event)
  {

    std::vector<uint8_t> tfeb_id = {0};
    std::map<uint8_t, std::vector<std::pair<int,float>>> tpesmap;
    tpesmap[0] = {std::make_pair(0,0)};

    int nHits = 0;

    if(fVerbose){
      std::cout<<"============================================"<<std::endl
               <<"Run = "<<event.run()<<", SubRun = "<<event.subRun()<<", Event = "<<event.id().event()<<std::endl
               <<"============================================"<<std::endl;
    }

    // Detector properties
    double readoutWindow  = (double)fDetectorProperties->ReadOutWindowSize();
    double driftTimeTicks = 2.0*(2.*fGeometryService->DetHalfWidth()+3.)/fDetectorProperties->DriftVelocity();

    // Retrieve list of CRT hits
    art::Handle< std::vector<crt::CRTData>> crtListHandle;
    std::vector<art::Ptr<crt::CRTData> > crtList;
    if (event.getByLabel(fCrtModuleLabel, crtListHandle))
      art::fill_ptr_vector(crtList, crtListHandle);

    // Create anab::T0 objects and make association with recob::Track
    std::unique_ptr< std::vector<crt::CRTHit> > CRTHitcol( new std::vector<crt::CRTHit>);

    // Fill a vector of pairs of time and width direction for each CRT plane
    // The y crossing point of z planes and z crossing point of y planes would be constant
    std::map<std::pair<std::string, unsigned>, std::vector<CRTStrip>> taggerStrips;

    if(fVerbose) std::cout<<"Number of SiPM hits = "<<crtList.size()<<"\n";

    // Loop over all the SiPM hits in 2 (should be in pairs due to trigger)
    for (size_t i = 0; i < crtList.size(); i+=2){
      // Get the time, channel, center and width
      double t1 = (double)(int)crtList[i]->T0()/8.;

      if(fUseReadoutWindow){
        if(!(t1 >= -driftTimeTicks && t1 <= readoutWindow)) continue;
      }

      uint32_t channel = crtList[i]->Channel();
      int strip = (channel >> 1) & 15;
      int module = (channel >> 5);
      std::string name = fGeometryService->AuxDet(module).TotalVolume()->GetName();
      TVector3 center = fAuxDetGeoCore->AuxDetChannelToPosition(2*strip, name);
      const geo::AuxDetSensitiveGeo stripGeo = fAuxDetGeoCore->ChannelToAuxDetSensitive(name, 2*strip);
      double width = 2*stripGeo.HalfWidth1();

      std::pair<std::string,unsigned> tagger = ChannelToTagger(channel);
      
      int id1 = crtList[i]->TrackID();
      int id2 = crtList[i+1]->TrackID(); 

      // Get the time of hit on the second SiPM
      double t2 = (double)(int)crtList[i+1]->T0()/8.;
      // Calculate the number of photoelectrons at each SiPM
      double npe1 = ((double)crtList[i]->ADC() - fQPed)/fQSlope;
      double npe2 = ((double)crtList[i+1]->ADC() - fQPed)/fQSlope;
      // Calculate the distance between the SiPMs
      double x = (width/2.)*atan(log(1.*npe2/npe1)) + (width/2.);

      // Calculate the error
      double normx = x + 0.344677*x - 1.92045;
      double ex = 1.92380e+00+1.47186e-02*normx-5.29446e-03*normx*normx;
      double time = (t1 + t2)/2.;

      CRTStrip stripHit = {time, channel, x, ex, id1, id2, npe1+npe2, tagger};
      taggerStrips[tagger].push_back(stripHit);

    }

    // Remove any duplicate (same channel and time) hit strips
    for(auto &tagStrip : taggerStrips){
      std::sort(tagStrip.second.begin(), tagStrip.second.end(),
                [](const CRTStrip & a, const CRTStrip & b) -> bool{
                  return (a.t0 < b.t0) || 
                         ((a.t0 == b.t0) && (a.channel < b.channel));
                });
      // Remove hits with the same time and channel
      tagStrip.second.erase(std::unique(tagStrip.second.begin(), tagStrip.second.end(),
                                           [](const CRTStrip & a, const CRTStrip & b) -> bool{
                                             return a.t0 == b.t0 && a.channel == b.channel;
                                            }), tagStrip.second.end());
    }

    std::vector<std::string> usedTaggers;

    for (auto &tagStrip : taggerStrips){
      if (std::find(usedTaggers.begin(),usedTaggers.end(),tagStrip.first.first)!=usedTaggers.end()) continue;
      usedTaggers.push_back(tagStrip.first.first);
      unsigned planeID = 0;
      if(tagStrip.first.second==0) planeID = 1;
      std::pair<std::string,unsigned> otherPlane = std::make_pair(tagStrip.first.first, planeID);
      for (size_t hit_i = 0; hit_i < tagStrip.second.size(); hit_i++){
        // Get the position (in real space) of the 4 corners of the hit, taking charge sharing into account
        std::vector<double> limits1 =  ChannelToLimits(tagStrip.second[hit_i]);
        // Check for overlaps on the first plane
        if(CheckModuleOverlap(tagStrip.second[hit_i].channel)){
          // Loop over all the hits on the parallel (odd) plane
          for (size_t hit_j = 0; hit_j < taggerStrips[otherPlane].size(); hit_j++){
            // Get the limits in the two variable directions
            std::vector<double> limits2 = ChannelToLimits(taggerStrips[otherPlane][hit_j]);
            // If the time and position match then record the pair of hits
            std::vector<double> overlap = CrtOverlap(limits1, limits2);
            double t0_1 = tagStrip.second[hit_i].t0;
            double t0_2 = taggerStrips[otherPlane][hit_j].t0;
            if (overlap[0] != -99999 && std::abs(t0_1 - t0_2)<fTimeCoincidenceLimit){
              // Calculate the mean and error in x, y, z
              TVector3 mean((overlap[0] + overlap[1])/2., 
                            (overlap[2] + overlap[3])/2., 
                            (overlap[4] + overlap[5])/2.);
              TVector3 error(std::abs((overlap[1] - overlap[0])/2.), 
                             std::abs((overlap[3] - overlap[2])/2.), 
                             std::abs((overlap[5] - overlap[4])/2.));
              // Average the time
              double time = (t0_1 + t0_2)/2;
              // Create a CRT hit
              crt::CRTHit crtHit = FillCrtHit(tfeb_id, tpesmap, 0, time, mean.X(), error.X(), mean.Y(), error.Y(), mean.Z(), error.Z(), tagStrip.first.first);
              CRTHitcol->push_back(crtHit);
              nHits++;
            }
          }
        }
        else{
          TVector3 mean((limits1[0] + limits1[1])/2., 
                        (limits1[2] + limits1[3])/2., 
                        (limits1[4] + limits1[5])/2.);
          TVector3 error(std::abs((limits1[1] - limits1[0])/2.), 
                         std::abs((limits1[3] - limits1[2])/2.), 
                         std::abs((limits1[5] - limits1[4])/2.));
          double time = tagStrip.second[hit_i].t0;
          // Just use the single plane limits as the crt hit
          crt::CRTHit crtHit = FillCrtHit(tfeb_id, tpesmap, 0, time, mean.X(), error.X(), mean.Y(), error.Y(), mean.Z(), error.Z(), tagStrip.first.first);
          CRTHitcol->push_back(crtHit);
          nHits++;
        }
      }
      for (size_t hit_j = 0; hit_j < taggerStrips[otherPlane].size(); hit_j++){
        // Get the limits in the two variable directions
        std::vector<double> limits1 = ChannelToLimits(taggerStrips[otherPlane][hit_j]);
        if(!CheckModuleOverlap(taggerStrips[otherPlane][hit_j].channel)){
          TVector3 mean((limits1[0] + limits1[1])/2., 
                        (limits1[2] + limits1[3])/2., 
                        (limits1[4] + limits1[5])/2.);
          TVector3 error(std::abs((limits1[1] - limits1[0])/2.), 
                         std::abs((limits1[3] - limits1[2])/2.), 
                         std::abs((limits1[5] - limits1[4])/2.));
          double time = taggerStrips[otherPlane][hit_j].t0;
          // Just use the single plane limits as the crt hit
          crt::CRTHit crtHit = FillCrtHit(tfeb_id, tpesmap, 0, time, mean.X(), error.X(), mean.Y(), error.Y(), mean.Z(), error.Z(), otherPlane.first);
          CRTHitcol->push_back(crtHit);
          nHits++;
        }
      }
    }

    event.put(std::move(CRTHitcol));

    if(fVerbose) std::cout<<"Number of CRT hits produced = "<<nHits<<std::endl;
    
  } // produce()

  void CRTSimHitProducer::endJob()
  {

  }

  // Function to calculate the strip position limits in real space from channel
  std::vector<double> CRTSimHitProducer::ChannelToLimits(CRTStrip stripHit){
    int strip = (stripHit.channel >> 1) & 15;
    int module = (stripHit.channel >> 5);
    std::string name = fGeometryService->AuxDet(module).TotalVolume()->GetName();
    const geo::AuxDetSensitiveGeo stripGeo = fAuxDetGeoCore->ChannelToAuxDetSensitive(name, 2*strip);
    double halfWidth = stripGeo.HalfWidth1();
    double halfHeight = stripGeo.HalfHeight();
    double halfLength = stripGeo.HalfLength();
    double l1[3] = {-halfWidth+stripHit.x+stripHit.ex, halfHeight, halfLength};
    double w1[3] = {0,0,0};
    stripGeo.LocalToWorld(l1, w1);
    double l2[3] = {-halfWidth+stripHit.x-stripHit.ex, -halfHeight, -halfLength};
    double w2[3] = {0,0,0};
    stripGeo.LocalToWorld(l2, w2);
    // Use this to get the limits in the two variable directions
    std::vector<double> limits = {std::min(w1[0],w2[0]), std::max(w1[0],w2[0]), 
                                  std::min(w1[1],w2[1]), std::max(w1[1],w2[1]), 
                                  std::min(w1[2],w2[2]), std::max(w1[2],w2[2])};
    return limits;
  } // ChannelToLimits


  // Function to calculate the overlap between two crt strips
  std::vector<double> CRTSimHitProducer::CrtOverlap(std::vector<double> strip1, std::vector<double> strip2){
    double minX = std::max(strip1[0], strip2[0]);
    double maxX = std::min(strip1[1], strip2[1]);
    double minY = std::max(strip1[2], strip2[2]);
    double maxY = std::min(strip1[3], strip2[3]);
    double minZ = std::max(strip1[4], strip2[4]);
    double maxZ = std::min(strip1[5], strip2[5]);
    std::vector<double> null = {-99999, -99999, -99999, -99999, -99999, -99999};
    std::vector<double> overlap = {minX, maxX, minY, maxY, minZ, maxZ};
    if ((minX<maxX && minY<maxY) || (minX<maxX && minZ<maxZ) || (minY<maxY && minZ<maxZ)) return overlap;
    return null;
  } // CRTRecoAna::CRTOverlap()

    std::pair<std::string,unsigned> CRTSimHitProducer::ChannelToTagger(uint32_t channel){
    int strip = (channel >> 1) & 15;
    int module = (channel >> 5);
    std::string name = fGeometryService->AuxDet(module).TotalVolume()->GetName();
    TVector3 center = fAuxDetGeoCore->AuxDetChannelToPosition(2*strip, name);
    const geo::AuxDetSensitiveGeo stripGeo = fAuxDetGeoCore->ChannelToAuxDetSensitive(name, 2*strip);

    std::set<std::string> volNames = {stripGeo.TotalVolume()->GetName()};
    std::vector<std::vector<TGeoNode const*> > paths = fGeometryService->FindAllVolumePaths(volNames);
    std::string path = "";
    for (size_t inode=0; inode<paths.at(0).size(); inode++) {
      path += paths.at(0).at(inode)->GetName();
      if (inode < paths.at(0).size() - 1) {
        path += "/";
      }
    }
    TGeoManager* manager = fGeometryService->ROOTGeoManager();
    manager->cd(path.c_str());
    TGeoNode* nodeModule = manager->GetMother(2);
    TGeoNode* nodeTagger = manager->GetMother(3);
    // Module position in parent (tagger) frame
    double origin[3] = {0, 0, 0};
    double modulePosMother[3];
    nodeModule->LocalToMaster(origin, modulePosMother);
    unsigned planeID = (modulePosMother[2] > 0);
    std::string tagName = nodeTagger->GetName();
    std::pair<std::string, unsigned> output = std::make_pair(tagName, planeID);
    return output;
  }

  // WARNING: Relies on all modules in a tagger having the same dimensions
  bool CRTSimHitProducer::CheckModuleOverlap(uint32_t channel){
    bool hasOverlap = false;
    // Get the module ID
    int strip = (channel >> 1) & 15;
    int module = (channel >> 5);
    // Get the name of the module
    std::string name = fGeometryService->AuxDet(module).TotalVolume()->GetName();
    // Get the tagger TGeoNode
    const geo::AuxDetSensitiveGeo stripGeo = fAuxDetGeoCore->ChannelToAuxDetSensitive(name, 2*strip);
    std::set<std::string> volNames = {stripGeo.TotalVolume()->GetName()};
    std::vector<std::vector<TGeoNode const*> > paths = fGeometryService->FindAllVolumePaths(volNames);
    std::string path = "";
    for (size_t inode=0; inode<paths.at(0).size(); inode++) {
      path += paths.at(0).at(inode)->GetName();
      if (inode < paths.at(0).size() - 1) {
        path += "/";
      }
    }
    TGeoManager* manager = fGeometryService->ROOTGeoManager();
    manager->cd(path.c_str());
    TGeoNode* nodeModule = manager->GetMother(2);
    TGeoNode* nodeTagger = manager->GetMother(3);
    std::string modName = nodeModule->GetName();
    // Get the limits of the module in the tagger frame
    double height = fGeometryService->AuxDet(module).HalfHeight();
    double width = fGeometryService->AuxDet(module).HalfWidth1();
    double length = fGeometryService->AuxDet(module).Length()/2.;
    double pos1[3] = {width, height, length};
    double tagp1[3];
    nodeModule->LocalToMaster(pos1, tagp1);
    double pos2[3] = {-width, -height, -length};
    double tagp2[3];
    nodeModule->LocalToMaster(pos2, tagp2);
    std::vector<double> limits = {std::min(tagp1[0],tagp2[0]),
                                  std::max(tagp1[0],tagp2[0]),
                                  std::min(tagp1[1],tagp2[1]),
                                  std::max(tagp1[1],tagp2[1]),
                                  std::min(tagp1[2],tagp2[2]),
                                  std::max(tagp1[2],tagp2[2])};
    double origin[3] = {0, 0, 0};
    double modulePosMother[3];
    nodeModule->LocalToMaster(origin, modulePosMother);
    unsigned planeID = (modulePosMother[2] > 0);

    // Get the number of daughters from the tagger
    int nDaughters = nodeTagger->GetNdaughters();
    // Loop over the daughters
    for(int mod_i = 0; mod_i < nDaughters; mod_i++){
      // Check the name not the same as the current module
      TGeoNode* nodeDaughter = nodeTagger->GetDaughter(mod_i);
      std::string d_name = nodeDaughter->GetName();
      // Remove last two characters from name to match the AuxDet name
      if(d_name == modName) continue;
      // Get the limits of the module in the tagger frame
      double d_tagp1[3];
      nodeDaughter->LocalToMaster(pos1, d_tagp1);
      double d_tagp2[3];
      nodeDaughter->LocalToMaster(pos2, d_tagp2);
      std::vector<double> d_limits = {std::min(d_tagp1[0],d_tagp2[0]),
                                      std::max(d_tagp1[0],d_tagp2[0]),
                                      std::min(d_tagp1[1],d_tagp2[1]),
                                      std::max(d_tagp1[1],d_tagp2[1]),
                                      std::min(d_tagp1[2],d_tagp2[2]),
                                      std::max(d_tagp1[2],d_tagp2[2])};
      double d_modulePosMother[3];
      nodeDaughter->LocalToMaster(origin, d_modulePosMother);
      unsigned d_planeID = (d_modulePosMother[2] > 0);

      // Check the overlap of the two modules
      std::vector<double> overlap = CrtOverlap(limits, d_limits);
      // If there is an overlap set to true
      if(overlap[0]!=-99999 && d_planeID!=planeID) hasOverlap = true;
    }
    return hasOverlap;
  }

  crt::CRTHit CRTSimHitProducer::FillCrtHit(std::vector<uint8_t> tfeb_id, std::map<uint8_t, std::vector<std::pair<int,float>>> tpesmap, float peshit, double time, double x, double ex, double y, double ey, double z, double ez, std::string tagger){
    crt::CRTHit crtHit;
    crtHit.feb_id = tfeb_id;
    crtHit.pesmap = tpesmap;
    crtHit.peshit = peshit;
    crtHit.ts0_s_corr = 0;
    crtHit.ts0_ns = time * 0.5 * 10e3;
    crtHit.ts0_ns_corr = 0;
    crtHit.ts1_ns = time * 0.5 * 10e3;
    crtHit.ts0_s = time * 0.5 * 10e-6; 
    crtHit.x_pos = x;
    crtHit.x_err = ex;
    crtHit.y_pos = y; 
    crtHit.y_err = ey;
    crtHit.z_pos = z;
    crtHit.z_err = ez;
    crtHit.tagger = tagger;
    return crtHit;
  }


  DEFINE_ART_MODULE(CRTSimHitProducer)

} // sbnd namespace

namespace {


}
