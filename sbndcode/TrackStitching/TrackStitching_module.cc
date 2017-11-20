////////////////////////////////////////////////////////////////////////
// Class:       TrackStitching
// Module Type: analyzer
// File:        TrackStitching_module.cc
//
// Tom Brooks (tbrooks@fnal.gov)
////////////////////////////////////////////////////////////////////////

// sbndcode includes
#include "sbndcode/RecoUtils/RecoUtils.h"

// LArSoft includes
#include "lardataobj/Simulation/SimChannel.h"
#include "lardataobj/RecoBase/Hit.h"
#include "lardataobj/RecoBase/Track.h"
#include "lardata/DetectorInfoServices/DetectorPropertiesService.h"
#include "larcore/Geometry/Geometry.h"
#include "larcorealg/Geometry/GeometryCore.h"
#include "larcoreobj/SimpleTypesAndConstants/geo_types.h"
#include "nusimdata/SimulationBase/MCParticle.h"
#include "nusimdata/SimulationBase/MCTruth.h"
#include "larsim/Simulation/LArG4Parameters.h"
#include "larsim/MCCheater/BackTracker.h"

// Framework includes
#include "art/Framework/Core/EDAnalyzer.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "art/Framework/Services/Optional/TFileService.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "canvas/Persistency/Common/FindManyP.h"
#include "canvas/Utilities/Exception.h"

// Utility libraries
#include "messagefacility/MessageLogger/MessageLogger.h"
#include "fhiclcpp/ParameterSet.h"
#include "fhiclcpp/types/Table.h"
#include "fhiclcpp/types/Atom.h"
#include "cetlib/pow.h" // cet::sum_of_squares()

// ROOT includes. Note: To look up the properties of the ROOT classes,
// use the ROOT web site; e.g.,
// <https://root.cern.ch/doc/master/annotated.html>
#include "TH1.h"
#include "TH2.h"
#include "TTree.h"
#include "TLorentzVector.h"
#include "TVector3.h"

// C++ includes
#include <map>
#include <vector>
#include <string>
#include <cmath>

namespace {
  // Local namespace for local functions
  // Declare here, define later

  // Utility function to determine if a true particle crosses the cathode
  bool CrossesCathode(simb::MCParticle const& particle);

  // Temp utility function to determine if a true particle hits a CRT
  bool HitsCRT(simb::MCParticle const& particle);

  // Temp utility function to determine if true particle triggers the PDS
  bool HitsPDS(simb::MCParticle const& particle, double lengthLimit);

  // Utility function to determine if tracks should be stitched
  int StitchTracks(recob::Track const& track1, recob::Track const& track2, double distLimit, double angleLimit);

}

namespace sbnd {
  class TrackStitching : public art::EDAnalyzer {
  public:

    // Describes configuration parameters of the module
    struct Config {
      using Name = fhicl::Name;
      using Comment = fhicl::Comment;
 
      // One Atom for each parameter
      fhicl::Atom<art::InputTag> SimulationLabel {
        Name("SimulationLabel"),
        Comment("tag of detector simulation data product")
      };
      
      fhicl::Atom<art::InputTag> TrackLabel {
        Name("TrackLabel"),
        Comment("tag of the input data product with reconstructed tracks")
      };

      fhicl::Atom<double> StitchAngle {
        Name("StitchAngle"),
        Comment("minimum angle to stitch tracks between TPCs (unit = degrees)")
      };

      fhicl::Atom<double> DeltaX {
        Name("DeltaX"),
        Comment("maximum difference in absolute x positions (unit = cm)")
      };
      
    }; // Config
 
    using Parameters = art::EDAnalyzer::Table<Config>;
 
    // Constructor: configures module
    explicit TrackStitching(Parameters const& config);
 
    // Called once, at start of the job
    virtual void beginJob() override;
 
    // Called once per event
    virtual void analyze(const art::Event& event) override;

    // Called once, at end of the job
    virtual void endJob() override;

  private:

    // fcl file parameters
    art::InputTag         fSimulationProducerLabel; ///< name of detsim producer
    art::InputTag         fTrackProducerLabel;      ///< name of the track producer
    double                fStitchAngle;             ///< minimum stitching angle between tracks
    double                fDeltaX;                  ///< maxmum difference in absolute x positions
    

    // Pointers to histograms
    TH1D* fCorrectAngleHist;    ///< Angle between correctly stitched tracks
    TH1D* fIncorrectAngleHist;  ///< Angle between incorrectly stitched tracks
    TH1D* fMissedAngleHist;     ///< Angle between tracks that should have been stitched but weren't
    TH1D* fCorrectDeltaXHist;   ///< Difference between min x positions for correctly stitched tracks
    TH1D* fIncorrectDeltaXHist; ///< Difference between min x positions for incorrectly stitched tracks
    TH1D* fMissedDeltaXHist;    ///< Difference between min x positions for missed tracks
    TH1D* fMissedMinLenHist;    ///< Shortest track length for missed tracks
    TH1D* fStartT;

    // The n-tuples
    TTree* fStitchingNtuple;     ///< tuple with info about stitching

    /// @name the variables that will go into both n-tuples.
    /// @{
    int fEvent;  ///< event number
    int fRun;    ///< run number
    int fSubRun; ///< sub-run number
    /// @}

    /// @name The variables that will go into the simulation n-tuple.
    /// @{
    int fNCathodeCrossers;  ///< Number of true primary particles crossing the cathode
    int fNCorrect;          ///< Number of correctly stitched reconstructed tracks
    int fNIncorrect;        ///< Number of incorrectly stitched reconstructed tracks
    int fNMissed;           ///< Number of missed reconstructed tracks
    /// @}

    // Other variables shared between different methods.
    geo::GeometryCore const* fGeometryService;                     ///< pointer to Geometry provider
    detinfo::DetectorProperties const* fDetectorProperties; ///< pointer to detector properties provider

    // Counters for text output
    int nCathodeCrossers = 0;
    int nCase1           = 0;
    int nCase2           = 0;
    int nCase3           = 0;
    int nStitched        = 0;
    int nCorrect         = 0;
    int nIncorrect       = 0;
    int nMissed          = 0;
  }; // class TrackStitching

  // Constructor
  TrackStitching::TrackStitching(Parameters const& config)
    : EDAnalyzer(config)
    , fSimulationProducerLabel(config().SimulationLabel())
    , fTrackProducerLabel     (config().TrackLabel())
    , fStitchAngle            (config().StitchAngle())
    , fDeltaX                 (config().DeltaX())
  {
    // Get a pointer to the geometry service provider
    fGeometryService = lar::providerFrom<geo::Geometry>();
    fDetectorProperties = lar::providerFrom<detinfo::DetectorPropertiesService>();
    
  }

  void TrackStitching::beginJob()
  {
    // Access tfileservice to handle creating and writing histograms
    art::ServiceHandle<art::TFileService> tfs;

    // Define histograms
    fCorrectAngleHist    = tfs->make<TH1D>("correctang",   ";Angle between tracks (rad);", 180, 0, 180);
    fIncorrectAngleHist  = tfs->make<TH1D>("incorrectang", ";Angle between tracks (rad);", 180, 0, 180);
    fMissedAngleHist     = tfs->make<TH1D>("missedang",    ";Angle between tracks (rad);", 180, 0, 180);
    fCorrectDeltaXHist   = tfs->make<TH1D>("correctdx",    ";#Delta x (cm);",              100, 0, 10);
    fIncorrectDeltaXHist = tfs->make<TH1D>("incorrectdx",  ";#Delta x (cm);",              100, 0, 10);
    fMissedDeltaXHist    = tfs->make<TH1D>("misseddx",     ";#Delta x (cm);",              100, 0, 10);
    fMissedMinLenHist    = tfs->make<TH1D>("missedlen",    ";Min track length (cm);",      100, 0, 200);
    fStartT = tfs->make<TH1D>("start","",100,-60000,60000);

    // Define n-tuples
    fStitchingNtuple = tfs->make<TTree>("TrackStitching", "TrackStitching");

    // Define branches of simulation n-tuple
    fStitchingNtuple->Branch("Event",           &fEvent,            "Event/I");
    fStitchingNtuple->Branch("SubRun",          &fSubRun,           "SubRun/I");
    fStitchingNtuple->Branch("Run",             &fRun,              "Run/I");
    fStitchingNtuple->Branch("CathodeCrossers", &fNCathodeCrossers, "CathodeCrossers/I");
    fStitchingNtuple->Branch("Correct",         &fNCorrect,         "Correct/I");
    fStitchingNtuple->Branch("Incorrect",       &fNIncorrect,       "Incorrect/I");
    fStitchingNtuple->Branch("Missed",          &fNMissed,          "Missed/I");

    std::cout<<"Drift velocity      = "<<fDetectorProperties->DriftVelocity()<<" cm/us"<<std::endl
             <<"Max drift distance  = "<<2.0*fGeometryService->DetHalfWidth()<<" cm"<<std::endl
             <<"Readout window size = "<<fDetectorProperties->ReadOutWindowSize()<<" ticks"<<std::endl
             <<"Max drift time      = "<<4.0*fGeometryService->DetHalfWidth()/fDetectorProperties->DriftVelocity()<<" ticks"<<std::endl;

  } // TrackStitchingbeginJob

  void TrackStitching::analyze(const art::Event& event)
  {
    // Initialise counters
    fNCathodeCrossers = 0;
    fNCorrect         = 0;
    fNIncorrect       = 0;
    fNMissed          = 0;

    std::vector<double> vCrtTimes; // Vector of true start times from cosmic rat trackers (units = ticks)
    std::vector<double> vPdsTimes; // Vector of true start times from photon detection system (units = ticks)

    // Fetch basic event info
    fEvent  = event.id().event();
    fRun    = event.run();
    fSubRun = event.subRun();

    // Get true particles
    // Define handle to point to a vector of MCParticles
    auto particleHandle = event.getValidHandle<std::vector<simb::MCParticle>>(fSimulationProducerLabel);
    std::map<int, simb::MCParticle> particles;
    // Loop over the true particles
    for (auto const& particle : (*particleHandle) ){
      int partId = particle.TrackId();
      particles[partId] = particle;

      double startTimeTicks = (particle.T()*10e-9)/(0.5*10e-6);
      double readoutWindow  = (double)fDetectorProperties->ReadOutWindowSize();
      double driftTimeTicks = 4.0*fGeometryService->DetHalfWidth()/fDetectorProperties->DriftVelocity();
      // If charged particle crosses CRTs and is within reconstructable window add the start time to a vector
      if (HitsCRT(particle) && startTimeTicks > -driftTimeTicks && startTimeTicks < readoutWindow){
        vCrtTimes.push_back(startTimeTicks);
      }
      // If charged particle has more than 5 cm inside AV and is within reconstructable window add the start time to a vector
      if (HitsPDS(particle, 5) && startTimeTicks > -driftTimeTicks && startTimeTicks < readoutWindow){
        vPdsTimes.push_back(startTimeTicks);
      }
    }
      
    // Get tracks from the event
    auto trackHandle = event.getValidHandle<std::vector<recob::Track>>(fTrackProducerLabel);
    // Get track to hit associations
    art::FindManyP<recob::Hit> findManyHits(trackHandle, event, fTrackProducerLabel);
    if ( ! findManyHits.isValid() ) {
      mf::LogError("TrackStitching")  
        << "findManyHits recob::Hit for recob::Track failed;"
        << " track label='" << fTrackProducerLabel << "'";
    }

    std::map<int ,const recob::Track*> tracksInTpc1;
    std::map<int, const recob::Track*> tracksInTpc2;
    int track_i = 0;
    // Loop over tracks
    for (auto const& track : (*trackHandle) ){
      // Put tracks in a map according to which tpc the hits are in
      std::vector< art::Ptr<recob::Hit> > hits = findManyHits.at(track_i);
      bool inTpc1 = false;
      bool inTpc2 = false;
      for (auto const& hit : hits){
        if (hit->WireID().TPC == 0) inTpc1 = true;
        if (hit->WireID().TPC == 1) inTpc2 = true;
      }
      // Hits all in first tpc
      if (inTpc1 && !inTpc2){
        tracksInTpc1[track_i] = &track;
      }
      // Hits all in second tpc
      else if (inTpc2 && !inTpc1){
        tracksInTpc2[track_i] = &track;
      }
      // If reconstructed track crossed the cathode (has hits in both tpcs) don't put in map
      track_i++;
    }
    
    // There are 3 distinct ways that tracks can be messed up when they cross the cathode plane depending on their true start times
    // Case 1 (-drift time < t < 0): Tracks shifted towards anodes
    // Case 2 (0 < t < dt): Tracks shifted into other TPC, crossing points reconstructed (dt = readout window - drift time)
    // Case 3 (dt < t < readout window): Tracks shifted into other TPC, crossing points not reconstructed

    // Loop over tracks in the first TPC
    for (auto const& tpc1Track : tracksInTpc1){
      auto track1 = tpc1Track.second;
      std::vector< art::Ptr<recob::Hit> > tpc1Hits = findManyHits.at(track1.ID());
      int tpc1TrueId = RecoUtils::TrueParticleIDFromTotalTrueEnergy(tpc1Hits);

      // Loop over tracks in second TPC
      for (auto const& tpc2Track : tracksInTpc2){
        auto track2 = tpc2Track.second;
        std::vector< art::Ptr<recob::Hit> > tpc2Hits = findManyHits.at(track2.ID());
        int tpc2TrueId = RecoUtils::TrueParticleIDFromTotalTrueEnergy(tpc2Hits);

        // If the start or end x points match within some tolerance mark as potential crosser
        double cosThr = cos(TMath::Pi() * fStitchAngle / 180.0);
        int stitchResult = StitchTracks(track1, track2, fDeltaX, cosThr);

        // If stitched, true particle ID same and true particle crosses cathode mark as correct, fill hist
        if (stitchResult != 0 && tpc1TrueId == tpc2TrueId && CrossesCathode(particles[tpc1TrueId])){
          fNCorrect++;
          nCorrect++;
          //fCorrectAngleHist->Fill(TMath::ACos(cos3d)*180./TMath::Pi());
          //fCorrectDeltaXHist->Fill(std::abs(tpc1MinX+tpc2MinX));
        }

        // If stitched, true particle IDs not equal and/or true particle doesn't cross cathode mark as incorrect, fill hist
        if (stitchResult != 0 && (tpc1TrueId != tpc2TrueId || !CrossesCathode(particles[tpc1TrueId]))){ 
          std::cout<<"Incorrect, event = "<<fEvent<<std::endl;
          fNIncorrect++;
          nIncorrect++;
          //fIncorrectAngleHist->Fill(TMath::ACos(cos3d)*180./TMath::Pi());
          //fIncorrectDeltaXHist->Fill(std::abs(tpc1MinX+tpc2MinX));
        }

        // If not stitched, true particle ID same and true particle crosses cathode mark as missed, fill hist
        if (stitchResult == 0 && tpc1TrueId == tpc2TrueId && CrossesCathode(particles[tpc1TrueId])){
          std::cout<<"Missed, event = "<<fEvent<<std::endl;
          fNMissed++;
          nMissed++;
          //fMissedAngleHist->Fill(TMath::ACos(cos3d)*180./TMath::Pi());
          //fMissedDeltaXHist->Fill(std::abs(tpc1MinX+tpc2MinX));
          //fMissedMinLenHist->Fill(std::min(track1->Length(),track2->Length()));
        }
        if (stitchResult != 0){
          nStitched++;
        }   
      }
    }

    // Loop over the matched
    for (auto const& match : matches){
      // For each track loop over over the other map and compare x positions
      
        // Find associated true particle for both tracks
        std::vector< art::Ptr<recob::Hit> > tpc1Hits = findManyHits.at(match.first.ID());
        int tpc1TrueId = RecoUtils::TrueParticleIDFromTotalTrueEnergy(tpc1Hits);
        std::vector< art::Ptr<recob::Hit> > tpc2Hits = findManyHits.at(match.second.ID());
        int tpc2TrueId = RecoUtils::TrueParticleIDFromTotalTrueEnergy(tpc2Hits);

        // If stitched, true particle ID same and true particle crosses cathode mark as correct, fill hist
        if (tpc1TrueId == tpc2TrueId && CrossesCathode(particles[tpc1TrueId])){
          fNCorrect++;
          nCorrect++;
          fCorrectAngleHist->Fill(TMath::ACos(cos3d)*180./TMath::Pi());
          fCorrectDeltaXHist->Fill(std::abs(tpc1MinX+tpc2MinX));
        }

        // If stitched, true particle IDs not equal and/or true particle doesn't cross cathode mark as incorrect, fill hist
        if (tpc1TrueId != tpc2TrueId || !CrossesCathode(particles[tpc1TrueId])){ 
          std::cout<<"Incorrect, event = "<<fEvent<<std::endl;
          fNIncorrect++;
          nIncorrect++;
          fIncorrectAngleHist->Fill(TMath::ACos(cos3d)*180./TMath::Pi());
          fIncorrectDeltaXHist->Fill(std::abs(tpc1MinX+tpc2MinX));
        }

        // If not stitched, true particle ID same and true particle crosses cathode mark as missed, fill hist
        if (!isStitched && tpc1TrueId == tpc2TrueId && CrossesCathode(particles[tpc1TrueId])){
          std::cout<<"Missed, event = "<<fEvent<<std::endl;
          fNMissed++;
          nMissed++;
          fMissedAngleHist->Fill(TMath::ACos(cos3d)*180./TMath::Pi());
          fMissedDeltaXHist->Fill(std::abs(tpc1MinX+tpc2MinX));
          fMissedMinLenHist->Fill(std::min(track1->Length(),track2->Length()));
        }

      } // End of loop over TPC 2 tracks
    
    } // End of loop over TPC 1 tracks

    fStitchingNtuple->Fill();

  } // TrackStitching::analyze()

  void TrackStitching::endJob(){
    // Output some variables
    std::cout<<"Number of true tracks crossing the cathode = "<<nCathodeCrossers<<std::endl
             <<"Number moved away from cathode             = "<<nCase1<<std::endl
             <<"Number moved towards cathode, not cut off  = "<<nCase2<<std::endl
             <<"Number moved towards cathode, cut off      = "<<nCase3<<std::endl
             <<"Total number of stitched reco tracks       = "<<nStitched<<std::endl
             <<"Number of correctly stitched reco tracks   = "<<nCorrect<<std::endl
             <<"Number of incorrectly stitched reco tracks = "<<nIncorrect<<std::endl
             <<"Number of missed reco tracks               = "<<nMissed<<std::endl;
  }

  DEFINE_ART_MODULE(TrackStitching)
} // namespace sbnd

// Back to our local namespace.
namespace {

  // Define a local function to determine if true track crosses cathode
  bool CrossesCathode(simb::MCParticle const& particle){
    size_t numTrajPoints = particle.NumberTrajectoryPoints();
    auto mcTrajectory = particle.Trajectory();
    bool inTpc1 = false;
    bool inTpc2 = false;
    // Loop over particle trajectory
    for (size_t traj_i = 0; traj_i < numTrajPoints; traj_i++){
      if (RecoUtils::IsInsideTPC(mcTrajectory.Position(traj_i).Vect(), 0.0) && mcTrajectory.X(traj_i) < 0) inTpc1 = true;
      if (RecoUtils::IsInsideTPC(mcTrajectory.Position(traj_i).Vect(), 0.0) && mcTrajectory.X(traj_i) > 0) inTpc2 = true;
      // If particle has two traj points inside tpc either side of cathode assume it's crossed
      if (inTpc1 && inTpc2) return true;
    }
    return false;
  } // CrossesCathode()

  // Function to check if particle is charged and crosses the TPC boundary
  bool HitsCRT(const simb::MCParticle& part){

    // Check particle is charged first
    int pdg = part.PdgCode();
    if (!(pdg == std::abs(13) || pdg == std::abs(11) || pdg == std::abs(2212) || pdg == std::abs(321) || pdg == std::abs(211))) return false;

    // Check if particle has points both inside and outside the active volume
    bool outsideAV = false;
    bool insideAV  = false;

    // Get geometry.
    art::ServiceHandle<geo::Geometry> geom;
    // Get active volume boundary. SBND specific
    double xmin = -2.0 * geom->DetHalfWidth();
    double xmax = 2.0 * geom->DetHalfWidth();
    double ymin = -geom->DetHalfHeight();
    double ymax = geom->DetHalfHeight();
    double zmin = 0.;
    double zmax = geom->DetLength();

    // Loop over trajectory points
    int nTrajPoints = part.NumberTrajectoryPoints();
    for (int traj_i = 0; traj_i < nTrajPoints; traj_i++){
      TVector3 trajPoint(part.Vx(traj_i), part.Vy(traj_i), part.Vz(traj_i));
      // Check if point is within reconstructable volume
      if (trajPoint[0] >= xmin && trajPoint[0] <= xmax && trajPoint[1] >= ymin && trajPoint[1] <= ymax && trajPoint[2] >= zmin && trajPoint[2] <= zmax){
        insideAV = true;
      }
      else outsideAV = true;
    }

    if(insideAV && outsideAV) return true;
    return false;

  } // HitsCRT()

  // Function to check if particle is charged and has a certain length inside TPC
  bool HitsPDS(const simb::MCParticle& part, double lengthLimit){

    // Check particle is charged first
    int pdg = part.PdgCode();
    if (!(pdg == std::abs(13) || pdg == std::abs(11) || pdg == std::abs(2212) || pdg == std::abs(321) || pdg == std::abs(211))) return false;

    // Calculate the length of the track inside the TPC
    double length = 0.;
    bool first  = true;
    TVector3 displacement;

    // Get geometry.
    art::ServiceHandle<geo::Geometry> geom;
    // Get active volume boundary. SBND specific
    double xmin = -2.0 * geom->DetHalfWidth();
    double xmax = 2.0 * geom->DetHalfWidth();
    double ymin = -geom->DetHalfHeight();
    double ymax = geom->DetHalfHeight();
    double zmin = 0.;
    double zmax = geom->DetLength();

    // Loop over trajectory points
    int nTrajPoints = part.NumberTrajectoryPoints();
    for (int traj_i = 0; traj_i < nTrajPoints; traj_i++){
      TVector3 trajPoint(part.Vx(traj_i), part.Vy(traj_i), part.Vz(traj_i));
      // Check if point is within reconstructable volume
      if (trajPoint[0] >= xmin && trajPoint[0] <= xmax && trajPoint[1] >= ymin && trajPoint[1] <= ymax && trajPoint[2] >= zmin && trajPoint[2] <= zmax){
        if(!first) {
          displacement -= trajPoint;
          length += displacement.Mag();
        }
        first = false;
        displacement = trajPoint;
      }
    }

    if (length > lengthLimit) return true;
    return false;

  } // HitsPDS()

  // Function to determine if tracks should be stitched
  int MatchPoints(recob::Track const& track1, recob::Track const& track2, double distLimit){

    // RETURN CODES:
    // 0 = not matched;
    // 11 = start matched with start
    // 12 = start matched with end, etc

    // Compare start and end x positions
    TVector3 position1, position2;
    TVector3 direction1, direction2;
    bool possibleMatch = false;
    double origLimit = distLimit;
    int code = 0;

    double difference = std::abs(std::abs(track1.Vertex().X())-std::abs(track2.Vertex().X()));
    if (difference < distLimit && track1.Vertex().X()*track2.Vertex().X < 0.0){
      position1 = track1.Vertex();
      position2 = track2.Vertex();
      direction1 = track1.VertexDirection();
      direction2 = track2.VertexDirection();
      distLimit = difference;
      possibleMatch = true;
      code = 11;
    }
    difference = std::abs(std::abs(track1.Vertex().X())-std::abs(track2.End().X()));
    if (difference < distLimit && track1.Vertex().X()*track2.End().X() < 0.0){
      position1 = track1.Vertex();
      position2 = track2.End();
      direction1 = track1.VertexDirection();
      direction2 = track2.EndDirection();
      distLimit = difference;
      possibleMatch = true;
      code = 12;
    }
    difference = std::abs(std::abs(track1.End().X())-std::abs(track2.Vertex().X()));
    if (difference < distLimit && track1.End().X()*track2.Vertex().X() < 0.0){
      position1 = track1.End();
      position2 = track2.Vertex();
      direction1 = track1.EndDirection();
      direction2 = track2.VertexDirection();
      distLimit = difference;
      possibleMatch = true;
      code = 21;
    }
    difference = std::abs(std::abs(track1.End().X())-std::abs(track2.End().X()));
    if (difference < distLimit && track1.End().X()*track2.End().X() < 0.0){
      position1 = track1.End();
      position2 = track2.End();
      direction1 = track1.EndDirection();
      direction2 = track2.EndDirection();
      possibleMatch = true;
      code = 22;
    }

    if (!possibleMatch) return 0;

    // If the matched positions are = difference between drift time and readout window mark as a case 3 match
    art::ServiceHandle<geo::Geometry> geom;
    auto detprop = lar::providerFrom<detinfo::DetectorPropertiesService>();
    double readoutWindow  = (double)detprop->ReadOutWindowSize();
    double driftTimeTicks = 4.0*geom->DetHalfWidth()/detprop->DriftVelocity();
    double dx = ((readoutWindow-driftTimeTicks)/0.5)*detprop->DriftVelocity();
    if (std::abs(position1.X()-dx) < origLimit) return 100 + code;

    // Calculate angle between tracks
    double cos3d = direction1.Dot(direction2);
    if (cos3d < angleLimit) return 200 + code;

  } // StitchTracks()

} // local namespace

