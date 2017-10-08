#include "mstfasst.h"

/* --------- FASST::optList --------- */
void FASST::optList::setOptions(const vector<mstreal>& _costs, bool add) {
  // sort costs and keep track of indices to know the rank-to-index mapping
  costs = _costs;
  rankToIdx.resize(_costs.size());
  for (int i = 0; i < rankToIdx.size(); i++) rankToIdx[i] = i;
  sort(rankToIdx.begin(), rankToIdx.end(), [this](int i, int j) { return costs[i] < costs[j]; });
  for (int i = 0; i < rankToIdx.size(); i++) costs[i] = _costs[rankToIdx[i]];

  // sort the the rank-to-index mapping and keep track of indices to know the index-to-rank mapping
  idxToRank.resize(costs.size());
  for (int i = 0; i < idxToRank.size(); i++) idxToRank[i] = i;
  sort(idxToRank.begin(), idxToRank.end(), [this](int i, int j) { return rankToIdx[i] < rankToIdx[j]; });

  // either include or exclude all options to start off, as instructed
  isIn.resize(costs.size(), add);
  if (add) {
    bestCostRank = 0;
    numIn = costs.size();
  } else {
    bestCostRank = -1;
    numIn = 0;
  }
}

void FASST::optList::addOption(int k) {
  // if ((k < 0) || (k >= costs.size())) MstUtils::error("out-of-range index specified: " + MstUtils::toString(k), "FASST::optList::insertOption(int)");
  if (!isIn[k]) {
    numIn++;
    isIn[k] = true;
    // update best, if the newly inserted option is better
    if ((bestCostRank < 0) || (costs[bestCostRank] > costs[idxToRank[k]])) {
      bestCostRank = idxToRank[k];
    }
  }
}

bool FASST::optList::consistencyCheck() {
  int nn = 0;
  for (int i = 0; i < isIn.size(); i++) {
    if (isIn[i]) nn++;
  }
  if (nn != numIn) {
    return false;
  }
  if (numIn == 0) {
    if (bestCostRank != -1) return false;
  } else {
    if ((bestCostRank < 0) || (bestCostRank >= costs.size())) return false;
    if (!isIn[rankToIdx[bestCostRank]]) return false;
  }
  return true;
}

void FASST::optList::removeOption(int k) {
  // if ((k < 0) || (k >= costs.size())) MstUtils::error("out-of-range index specified: " + MstUtils::toString(k), "FASST::optList::removeOption(int)");
  if (isIn[k]) {
    numIn--;
    isIn[k] = false;
    // find new best, if this was the best
    if (bestCostRank == idxToRank[k]) {
      int oldBestRank = bestCostRank;
      bestCostRank = -1;
      for (int r = oldBestRank + 1; r < costs.size(); r++) {
        if (isIn[rankToIdx[r]]) { bestCostRank = r; break; }
      }
    }
  }
}

void FASST::optList::removeOptions(int b, int e) {
  if (b < 0) b = 0;
  if (e >= totNumOptions()) e = totNumOptions() - 1;
  for (int k = b; k <= e; k++) removeOption(k);
}

void FASST::optList::removeAllOptions() {
  for (int i = 0; i < isIn.size(); i++) isIn[i] = false;
  numIn = 0;
  bestCostRank = -1;
}

void FASST::optList::copyIn(const FASST::optList& opt) {
  isIn = opt.isIn;
  bestCostRank = opt.bestCostRank;
  numIn = opt.numIn;
}

void FASST::optList::intersectOptions(const vector<int>& opts) {
  vector<bool> isGiven(isIn.size(), false);
  for (int i = 0; i < opts.size(); i++) isGiven[opts[i]] = true;
  for (int i = 0; i < isIn.size(); i++) {
    if (isIn[i] && !isGiven[i]) removeOption(i);
  }
}

void FASST::optList::constrainLE(int idx) {
  for (int i = idx+1; i < isIn.size(); i++) removeOption(i);
}

void FASST::optList::constrainGE(int idx) {
  for (int i = 0; i < MstUtils::min(idx, (int) isIn.size()); i++) removeOption(i);
}

/* --------- FASST --------- */
FASST::FASST() {
  recLevel = 0;
  setRMSDCutoff(1.0);
  setSearchType(searchType::FULLBB);
  querySize = 0;
  updateGrids = false;
  gridSpacing = 15.0;
  memSave = false;
  maxNumMatches = minNumMatches = -1;
  gapConstSet = false;
}

FASST::~FASST() {
  // need to delete atoms only on the lowest level of recursion, because at
  // higher levels we point to the same atoms
  if (targetMasks.size()) targetMasks.back().deletePointers();
  for (int i = 0; i < targetStructs.size(); i++) delete targetStructs[i];
}

void FASST::setCurrentRMSDCutoff(mstreal cut) {
  rmsdCut = cut;
  residualCut = cut*cut*querySize;
}

void FASST::setMaxNumMatches(int _max) {
  maxNumMatches = _max;
  if ((minNumMatches >= 0) && (minNumMatches > maxNumMatches)) MstUtils::error("invalid pairing of min and max number of matches: " + MstUtils::toString(minNumMatches) + " and " + MstUtils::toString(maxNumMatches), "FASST::setMaxNumMatches");
}

void FASST::setMinNumMatches(int _min) {
  minNumMatches = _min;
  if ((maxNumMatches >= 0) && (minNumMatches > maxNumMatches)) MstUtils::error("invalid pairing of min and max number of matches: " + MstUtils::toString(minNumMatches) + " and " + MstUtils::toString(maxNumMatches), "FASST::setMinNumMatches");
}

void FASST::setMinGap(int i, int j, int gapLim) {
  if (gapLim < 0) MstUtils::error("gap constraints must be defined in the non-negative direction", "FASST::setMinGap");
  minGap[i][j] = gapLim;
  minGapSet[i][j] = true;
  gapConstSet = true;
}

void FASST::setMaxGap(int i, int j, int gapLim) {
  if (gapLim < 0) MstUtils::error("gap constraints must be defined in the non-negative direction", "FASST::setMinGap");
  maxGap[i][j] = gapLim;
  maxGapSet[i][j] = true;
  gapConstSet = true;
}

void FASST::resetGapConstraints() {
  minGap.resize(query.size(), vector<int>(query.size(), 0));
  maxGap = minGap;
  minGapSet.resize(query.size(), vector<bool>(query.size(), false));
  maxGapSet = minGapSet;
  gapConstSet = false;
}

bool FASST::validateSearchRequest() {
  if (query.size() != minGap.size()) {
    MstUtils::error("gap constraints inconsistent with number of segments in query", "FASST::validateSearchRequest()");
  }
  return true;
}

/* This function sets up the query, in the process deciding which part of the
 * query is really searchable (e.g., backbone). Various search types an be added
 * in the future to provide search capabilities over different parts of the
 * structure. The current implementation stipulates that the searchable part
 * involve the same number of atoms for each residue, so that atoms can be
 * stored in flat arrays for efficiency. But these could presumably be
 * interpreted differently. E.g., only some residue matches (parents of atoms)
 * may be accepted or some of the atoms can be dummy/empty ones. */
void FASST::setQuery(const string& pdbFile) {
  queryStruct.reset();
  queryStruct.readPDB(pdbFile);
  processQuery();
}

void FASST::setQuery(const Structure& Q) {
  queryStruct = Q;
  processQuery();
}

void FASST::processQuery() {
  querySize = 0;
  // auto-splitting segments by connectivity, but can do diffeerntly
  queryStruct = queryStruct.reassignChainsByConnectivity();
  query.resize(queryStruct.chainSize());
  for (int i = 0; i < queryStruct.chainSize(); i++) {
    query[i].resize(0);
    if (!parseChain(queryStruct[i], query[i])) {
      MstUtils::error("could not set query, because some atoms for the specified search type were missing", "FASST::setQuery");
    }
    MstUtils::assert(query[i].size() > 0, "query contains empty segment(s)", "FASST::setQuery");
    querySize += query[i].size();
  }
  currAlignment.resize(query.size(), -1);

  // // center-to-center distances in the qyery
  // centToCentDist.resize(query.size(), vector<mstreal>(query.size(), 0));
  // for (int i = 0; i < query.size(); i++) {
  //   for (int j = i+1; j < query.size(); j++) {
  //     centToCentDist[i][j] = query[i].getGeometricCenter().distancenc(query[j].getGeometricCenter());
  //     centToCentDist[j][i] = centToCentDist[i][j];
  //   }
  // }

  // the distance from the centroid of each segment and the centroid of the
  // previous segments considered together
  centToCentDist.resize(query.size(), vector<mstreal>(query.size(), 0));
  // segCentToPrevSegCentDist.resize(query.size(), 0.0); CartesianPoint cp(0, 0, 0);
  CartesianPoint C(0, 0, 0);
  int N = 0;
  for (int L = 0; L < query.size(); L++) {
    CartesianPoint ci = query[L].getGeometricCenter();
    int n = query[L].size();
    C = (C*N + ci*n)/(N + n);
    for (int i = L + 1; i < query.size(); i++) {
      centToCentDist[L][i] = C.distance(query[i].getGeometricCenter());
    }
    N += n;
    // segCentToPrevSegCentDist[i] = cp.distance(ci);
    // cp = ci;
  }

  // set query masks (subsets of query segments involved at each recursion level)
  queryMasks.clear(); queryMasks.resize(query.size());
  for (int i = 0; i < query.size(); i++) {
    for (int L = i; L < query.size(); L++) {
      for (int j = 0; j < query[i].size(); j++) {
        queryMasks[L].push_back(query[i][j]);
      }
    }
  }
  updateGrids = true;

  // // room for storing query segment centroids at each recursion level
  // mstreal xc, yc, zc;
  // for (int i = 0; i < querySegCents.size(); i++) querySegCents[i].deletePointers();
  // querySegCents.resize(query.size());
  // for (int i = 0; i < querySegCents.size(); i++) {
  //   querySegCents[i].resize(i+1);
  //   for (int j = 0; j <= i; j++) {
  //     query[j].getGeometricCenter(xc, yc, zc);
  //     querySegCents[i][j] = new Atom(1, "CEN" + MstUtils::toString(j), xc, yc, zc, 0.0, 1.0, true);
  //     querySegCents[i][j]->addAlternative(xc, yc, zc, 0.0, 1.0);
  //   }
  // }

  // set gap constraints structure
  resetGapConstraints();
}

void FASST::addTarget(const string& pdbFile) {
  Structure* targetStruct = new Structure(pdbFile, "QUIET");
  targetSource.push_back(targetInfo(pdbFile, targetFileType::PDB, 0, memSave));
  addTargetStructure(targetStruct);
}

void FASST::addTarget(const Structure& T) {
  Structure* targetStruct = new Structure(T);
  targetSource.push_back(targetInfo(T.getName(), targetFileType::STRUCTURE, 0, memSave));
  addTargetStructure(targetStruct);
}

void FASST::addTargetStructure(Structure* targetStruct) {
  if (memSave) stripSidechains(*targetStruct);
  targetStructs.push_back(targetStruct);
  targets.push_back(AtomPointerVector());
  AtomPointerVector& target = targets.back();
  // we don't care about the chain topology of the target, so append all residues
  for (int i = 0; i < targetStruct->chainSize(); i++) {
    parseChain(targetStruct->getChain(i), target);
  }
  MstUtils::assert(target.size() > 0, "empty target named '" + targetStruct->getName() + "'", "FASST::addTargetStructure");

  // orient the target structure in common frame (remember the transform)
  tr.push_back(TransformFactory::translate(-target.getGeometricCenter()));
  tr.back().apply(*targetStruct);

  // update extent for when will be creating proximity search objects
  mstreal _xlo, _ylo, _zlo, _xhi, _yhi, _zhi;
  ProximitySearch::calculateExtent(*targetStruct, _xlo, _ylo, _zlo, _xhi, _yhi, _zhi);
  if (targetStructs.size() == 1) {
    xlo = _xlo; ylo = _ylo; zlo = _zlo;
    xhi = _xhi; yhi = _yhi; zhi = _zhi;
  } else {
    xlo = min(xlo, _xlo); ylo = min(ylo, _ylo); zlo = min(zlo, _zlo);
    xhi = max(xhi, _xhi); yhi = max(yhi, _yhi); zhi = max(zhi, _zhi);
  }
  updateGrids = true;
}

void FASST::addTargets(const vector<string>& pdbFiles) {
  for (int i = 0; i < pdbFiles.size(); i++) addTarget(pdbFiles[i]);
}

bool FASST::parseChain(const Chain& C, AtomPointerVector& searchable) {
  bool foundAll = true;
  for (int i = 0; i < C.residueSize(); i++) {
    Residue& res = C.getResidue(i);
    switch(type) {
      case searchType::CA:
      case searchType::FULLBB: {
        AtomPointerVector bb;
        for (int k = 0; k < searchableAtomTypes.size(); k++) {
          Atom* a = NULL;
          for (int kk = 0; kk < searchableAtomTypes[k].size(); kk++) {
            if ((a = res.findAtom(searchableAtomTypes[k][kk], false)) != NULL) break;
          }
          if (a == NULL) { foundAll = false; break; }
          bb.push_back(a);
        }
        if (bb.size() == searchableAtomTypes.size()) searchable.insert(searchable.end(), bb.begin(), bb.end());
        break;
      }
      default:
        MstUtils::error("uknown search type '" + MstUtils::toString(type) + "' specified", "FASST::parseStructure");
    }
  }
  return foundAll;
}

void FASST::writeDatabase(const string& dbFile) {
  fstream ofs; MstUtils::openFile(ofs, dbFile, fstream::out | fstream::binary, "FASST::writeDatabase");
  for (int i = 0; i < targetStructs.size(); i++) targetStructs[i]->writeData(ofs);
  ofs.close();
}

void FASST::readDatabase(const string& dbFile) {
  fstream ifs; MstUtils::openFile(ifs, dbFile, fstream::in | fstream::binary, "FASST::readDatabase");
  while (ifs.peek() != EOF) {
    Structure* targetStruct = new Structure();
    int loc = ifs.tellg();
    targetStruct->readData(ifs);
    targetSource.push_back(targetInfo(dbFile, targetFileType::BINDATABASE, loc, false));
    addTargetStructure(targetStruct);
  }
  ifs.close();
}

void FASST::setSearchType(searchType _searchType) {
  type = _searchType;
  switch(type) {
    case searchType::CA:
      searchableAtomTypes =  {{"CA"}};
      break;
    case searchType::FULLBB:
      searchableAtomTypes =  {{"N", "NT"}, {"CA"}, {"C"}, {"O", "OT1", "OT2", "OXT"}};
      break;
    default:
      MstUtils::error("uknown search type '" + MstUtils::toString(type) + "' specified", "FASST::setSearchType");
  }
  atomsPerRes = searchableAtomTypes.size();
}

void FASST::stripSidechains(Structure& S) {
  for (int i = 0; i < S.chainSize(); i++) {
    Chain& C = S[i];
    for (int j = 0; j < C.residueSize(); j++) {
      Residue& R = C[j];
      int n = R.atomSize();
      for (int k = 0; k < n; k++) {
        char* name = R[k].getNameC();
        if (strlen(name)) { // check that it's okay to index atom name string
          switch (name[0]) {
            case 'N':
              if (!strcmp(name, "N") || !strcmp(name, "NT")) continue;
              break;
            case 'C':
              if (!strcmp(name, "C") || !strcmp(name, "CA")) continue;
              break;
            case 'O':
              if (!strcmp(name, "O") || !strcmp(name, "OT1") || !strcmp(name, "OT2") || !strcmp(name, "OXT")) continue;
              break;
          }
        }
        R.deleteAtom(k);
        k--; n--;
      }
      R.compactify();
    }
  }
}

void FASST::rebuildProximityGrids() {
  if (xlo == xhi) { xlo -= gridSpacing/2; xhi += gridSpacing/2; }
  if (ylo == yhi) { ylo -= gridSpacing/2; yhi += gridSpacing/2; }
  if (zlo == zhi) { zlo -= gridSpacing/2; zhi += gridSpacing/2; }
  int N = int(ceil(max(max((xhi - xlo), (yhi - ylo)), (zhi - zlo))/gridSpacing));
  ps.clear(); ps.resize(query.size());
  for (int i = 0; i < query.size(); i++) {
    ps[i] = ProximitySearch(xlo, ylo, zlo, xhi, yhi, zhi, N);
  }
  updateGrids = false;
}

void FASST::prepForSearch(int ti) {
  recLevel = 0;
  AtomPointerVector& target = targets[ti];
  if ((query.size() == 0) || (target.size() == 0)) {
    MstUtils::error("query and target must be set before starting search", "FASST::prepForSearch");
  }

  // if the search database has changed since the last search, update
  if (updateGrids) rebuildProximityGrids();

  // align every segment onto every admissible location on the target
  mstreal xc, yc, zc;
  segmentResiduals.resize(query.size());
  for (int i = 0; i < query.size(); i++) {
    ps[i].dropAllPoints();
    AtomPointerVector& seg = query[i];
    int Na = atomToResIdx(target.size()) - atomToResIdx(seg.size()) + 1; // number of possible alignments
    segmentResiduals[i].resize(MstUtils::max(Na, 0));
    for (int j = 0; j < Na; j++) {
      // NOTE: can save on this in several ways:
      // 1. the centroid calculation is effectively already done inside RMSDCalculator::bestRMSD
      // 2. updating just one atom involves a simple centroid adjustment, rather than recalculation
      // 3. is there a speedup to be gained from re-calculating RMSD with one atom updated only?
      AtomPointerVector targSeg = target.subvector(resToAtomIdx(j), resToAtomIdx(j) + query[i].size());
      segmentResiduals[i][j] = RC.bestResidual(query[i], targSeg);
      targSeg.getGeometricCenter(xc, yc, zc);
      if (query.size() > 1) ps[i].addPoint(xc, yc, zc, j);
    }
  }

  // initialize remOptions; all options are available at top level
  remOptions.clear(); remOptions.resize(query.size());
  for (int L = 0; L < query.size(); L++) {
    remOptions[L].resize(query.size());
    for (int i = L; i < query.size(); i++) {
      // remOptions[L][i], where i < L, will hold an empty list of options, but
      // it won't be used and it is more convenient to access without thinking
      // of offsets (a tiny bit of memory waste for convenience)
      remOptions[L][i].setOptions(segmentResiduals[i], L == 0);
    }
  }

  // current residual starts with 0, since nothing is aligned yet
  currResidual = 0;
  currResiduals.resize(query.size(), 0);
  currRemBound = boundOnRemainder(true);

  // // initialize center-to-center tolerances
  // ccTol.clear(); ccTol.resize(query.size());
  // for (int i = 0; i < query.size(); i++) {
  //   ccTol[i].resize(query.size(), vector<mstreal>(query.size(), -1.0)); // unnecessary (unused) entries will stay as -1
  // }

  // make room for target atom masks at different recursion levels
  if (targetMasks.size()) targetMasks.back().deletePointers();
  targetMasks.clear(); targetMasks.resize(query.size());
  for (int i = 0; i < query.size(); i++) {
    for (int L = i; L < query.size(); L++) {
      for (int j = 0; j < query[i].size(); j++) {
        if (L > i) {
          // for any repeated atoms on this level, make sure to point to the
          // same atoms as the mask on the previous level
          targetMasks[L].push_back(targetMasks[L-1][targetMasks[L-1].size() - query[i].size() + j]);
        } else {
          // and only create new atoms for the last segment
          targetMasks[L].push_back(new Atom(*(query[i][j])));
        }
      }
    }
  }

  // room for centroids of aligned sub-structure, at each recursion level
  currCents.clear();
  currCents.resize(query.size(), CartesianPoint(0, 0, 0));
}

mstreal FASST::boundOnRemainder(bool compute) {
  if (compute) {
    currRemBound = 0;
    for (int i = recLevel + 1; i < query.size(); i++) currRemBound += remOptions[recLevel][i].bestCost();
  }
  return currRemBound;
}

mstreal FASST::centToCentTol(int i) {
  mstreal remRes = residualCut - currResidual - currRemBound;
  if (remRes < 0) return -1.0;
  return sqrt((remRes * (queryMasks[recLevel].size() + query[i].size())) / (queryMasks[recLevel].size() * query[i].size()));
}

mstreal FASST::segCentToPrevSegCentTol(int i) {
  int p = recLevel; // index of the previously placed segment

  /* Say the distance between the centroid of some to-be-placed segment i and
   * the segment p that was just placed is different from the corresponding
   * distance in the query by D. Then, we are guaranteed that the residual from
   * i and p in the final alignment will be at least Si + Sp + D^2 * (Ni * Np)/(Ni + Np),
   * where Si and Sp are the residuals from aligning i and p on their own, and
   * Ni and Np are the numbers of atoms in these segments. We also know that
   * we will have at least some residual from subsequent segment placements:
   *  + currRemBound = Si + all subsequent self-residuals
   *  + segmentResiduals[p][currAlignment[p]] = Sp
   * And, we also know the residual from placing all segments prior to p together:
   *  + currResiduals[p - 1]
   * (note that if there are segments prior to p, then recLevel is at least 2).
   * Thus, D^2 * (Ni * Np)/(Ni + Np) must be no larger than the difference between
   * residualCut (maximal allowed residual) and the three above components. */
  mstreal remRes = residualCut - currRemBound - segmentResiduals[p][currAlignment[p]];
  if (p > 0) {
    remRes -= currResiduals[p - 1];
  }
  if (remRes < 0) return -1.0;
  return sqrt((remRes * (query[p].size() + query[i].size())) / (query[p].size() * query[i].size()));
}

void FASST::search() {
  // auto begin = chrono::high_resolution_clock::now();
  // int prepTime = 0;
  validateSearchRequest();
  if (isMinNumMatchesSet()) setCurrentRMSDCutoff(999.0);
  else setCurrentRMSDCutoff(rmsdCutRequested);
  solutions.clear();
  vector<int> segLen(query.size()); // number of residues in each query segment
  for (int i = 0; i < query.size(); i++) segLen[i] = atomToResIdx(query[i].size());
  vector<mstreal> ccTol(query.size(), -1.0);
  for (currentTarget = 0; currentTarget < targets.size(); currentTarget++) {
    // auto beginPrep = chrono::high_resolution_clock::now();
    prepForSearch(currentTarget);
    // auto endPrep = chrono::high_resolution_clock::now();
    // prepTime += chrono::duration_cast<std::chrono::microseconds>(endPrep-beginPrep).count();
    vector<int> okLocations, badLocations;
    okLocations.reserve(targets[currentTarget].size()); badLocations.reserve(targets[currentTarget].size());
    while (true) {
      // Have to do three things:
      // 1. pick the best choice (from available ones) for the current segment,
      // remove it from the list of options, and move onto the next recursion level
      if (remOptions[recLevel][recLevel].empty()) {
        currAlignment[recLevel] = -1;
        if (recLevel > 0) {
          recLevel--;
          continue;
        } else {
          break; // search exhausted
        }
      }
      currAlignment[recLevel] = remOptions[recLevel][recLevel].bestChoice();
      remOptions[recLevel][recLevel].removeOption(currAlignment[recLevel]);

      // 2. compute the total residual from the current alignment
      mstreal curBound = currentAlignmentResidual(true) + boundOnRemainder(true);
      if (curBound > residualCut) continue;
      // if (query.size() > 1) updateQueryCentroids();

      // 3. update update remaining options for subsequent segments based on the
      // newly made choice. The set of options on the next recursion level is a
      // subset of the set of options on the previous level.
      int remSegs = query.size() - (recLevel + 1);
      if (remSegs > 0) {
        bool levelExhausted = false;
        int nextLevel = recLevel + 1;
        // copy remaining options from the previous recursion level. This way,
        // we can compute bounds on this level and can do set intersections to
        // further narrow this down
        for (int i = nextLevel; i < query.size(); i++) {
          remOptions[nextLevel][i].copyIn(remOptions[nextLevel-1][i]);
          // except that segments cannot overlap, so remove from consideration
          // all alignments that overlap with the segments that was just placed
          remOptions[nextLevel][i].removeOptions(currAlignment[recLevel] - segLen[i] + 1,
                                                currAlignment[recLevel] + segLen[recLevel] - 1);
        }
        // if any gap constraints exist, limit options at this recursion level accordingly
        if (gapConstraintsExist()) {
          for (int j = 0; j < nextLevel; j++) {
            for (int i = nextLevel; i < query.size(); i++) {
              if (minGapSet[i][j]) remOptions[nextLevel][i].constrainLE(currAlignment[j] - minGap[i][j] - segLen[i]);
              if (maxGapSet[i][j]) remOptions[nextLevel][i].constrainGE(currAlignment[j] - maxGap[i][j] - segLen[i]);
              if (minGapSet[j][i]) remOptions[nextLevel][i].constrainGE(currAlignment[j] + minGap[j][i] + segLen[j]);
              if (maxGapSet[j][i]) remOptions[nextLevel][i].constrainLE(currAlignment[j] + maxGap[j][i] + segLen[j]);
              if (remOptions[nextLevel][i].empty()) { levelExhausted = true; break; }
            }
            if (levelExhausted) break;
          }
          if (levelExhausted) continue;
        }
        mstreal di, de, d, dePrev, eps = 10E-8;
        CartesianPoint& currCent = currCents[recLevel];
        for (int c = 0; true; c++) {
          bool updated = false;
          for (int i = nextLevel; i < query.size(); i++) {
            FASST::optList& remSet = remOptions[nextLevel][i];
            de = centToCentTol(i);
            if (de < 0) { levelExhausted = true; break; }
            di = centToCentDist[recLevel][i];
            dePrev = ((c == 0) ? -1 : ccTol[i]);
            int numLocs = remSet.size();

            // If the set of options for the current segment was arrived at,
            // in part, by limiting center-to-center distances, then we will
            // tighten that list by removing options that are outside of the
            // range allowed at this recursion level. Otherwise, we will do a
            // general proximity search given the current tolerance and will
            // tighten the list that way.
            if (dePrev < 0) {
              okLocations.resize(0);
              ps[i].pointsWithin(currCent, max(di - de, 0.0), di + de, &okLocations);
              remSet.intersectOptions(okLocations);
            } else if (dePrev - de > eps) {
              badLocations.resize(0);
              ps[i].pointsWithin(currCent, max(di - dePrev, 0.0), di - de - eps, &badLocations);
              ps[i].pointsWithin(currCent, di + de + eps, di + dePrev, &badLocations);
              for (int k = 0; k < badLocations.size(); k++) {
                remSet.removeOption(badLocations[k]);
              }
            }
            ccTol[i] = de;
            if (numLocs != remSet.size()) {
              // this both updates the bound and checks that there are still
              // feasible solutions left
              if ((remSet.empty()) || (currResidual + boundOnRemainder(true) > residualCut)) {
                levelExhausted = true; break;
              }
              updated = true;
            }
          }
          if (levelExhausted) break;
          if (!updated) break;
        }
        if (levelExhausted) continue;
        recLevel = nextLevel;
      } else {
        // if at the lowest recursion level already, then record the solution
        solutions.insert(fasstSolution(currAlignment, sqrt(currResidual/querySize), currentTarget, solutions.size()));
        // if ((maxNumMatches > 0) && (solutions.size() > maxNumMatches)) {
        if (isMaxNumMatchesSet() && (solutions.size() > maxNumMatches)) {
          solutions.erase(--solutions.end());
          setCurrentRMSDCutoff(solutions.rbegin()->getRMSD());
        } else if (isMinNumMatchesSet() && (solutions.size() > minNumMatches) && (rmsdCut > rmsdCutRequested)) {
          if (solutions.rbegin()->getRMSD() > rmsdCutRequested) solutions.erase(--solutions.end());
          setCurrentRMSDCutoff(solutions.rbegin()->getRMSD());
        }
      }
    }
  }
  // auto end = chrono::high_resolution_clock::now();
  // int searchTime = chrono::duration_cast<std::chrono::milliseconds>(end-begin).count();
  // prepTime = prepTime/1000;
  // cout << "prep time " << prepTime << " ms" << std::endl;
  // cout << "total time " << searchTime << " ms" << std::endl;
  // cout << "prep time was " << (100.0*prepTime/searchTime) << " % of the total" << std::endl;
}

mstreal FASST::currentAlignmentResidual(bool compute) {
  if (compute) {
    if (query.size() == 1) {
      // this is a special case, because will not need to calculate centroid
      // locations for subsequent sub-queries
      currResidual = segmentResiduals[0][currAlignment[0]];
    } else {
      // fill up sub-alignment with target atoms
      int N = targetMasks[recLevel].size();
      int n = query[recLevel].size();
      int si = resToAtomIdx(currAlignment[recLevel]);
      AtomPointerVector& target = targets[currentTarget];
      for (int i = 0; i < n; i++) {
        targetMasks[recLevel][N - n + i]->setCoor(target[si + i]->getX(), target[si + i]->getY(), target[si + i]->getZ());
      }
      currResidual = RC.bestResidual(queryMasks[recLevel], targetMasks[recLevel]);
      currResiduals[recLevel] = currResidual;
      if (recLevel == 0) {
        currCents[recLevel] = ps[recLevel].getPoint(currAlignment[recLevel]);
      } else {
        currCents[recLevel] = (currCents[recLevel - 1] * (N - n) +  ps[recLevel].getPoint(currAlignment[recLevel]) * n) / N;
      }
    }
  }
  return currResidual;
}

// void FASST::updateQueryCentroids() {
//   AtomPointerVector& cents = querySegCents[recLevel];
//   for (int i = 0; i < cents.size(); i++) cents[i]->makeAlternativeMain(0); // first, return the original coordinates
//   RC.applyLastTransformation(cents); // then, apply transformation
// }

void FASST::getMatchStructure(const fasstSolution& sol, Structure& match, bool detailed, matchType type) {
  vector<Structure> matches;
  getMatchStructures(vector<fasstSolution>(1, sol), matches, detailed, type);
  match = matches[0];
}

Structure FASST::getMatchStructure(const fasstSolution& sol, bool detailed, matchType type) {
  Structure match; getMatchStructure(sol, match, detailed, type); return match;
}

void FASST::getMatchStructures(const set<fasstSolution>& sols, vector<Structure>& matches, bool detailed, matchType type) {
  vector<fasstSolution> vec; vec.reserve(sols.size());
  int i = 0;
  for (auto it = sols.begin(); it != sols.end(); ++it, ++i) vec[i] = *it;
  getMatchStructures(vec, matches, detailed, type);
}

void FASST::getMatchStructures(const vector<fasstSolution>& sols, vector<Structure>& matches, bool detailed, matchType type) {
  // hash solutions by the target they come from, to visit each target only once
  map<int, vector<int> > solsFromTarget;
  for (int i = 0; i < sols.size(); i++) {
    const fasstSolution& sol = sols[i];
    int idx = sol.getTargetIndex();
    if ((idx < 0) || (idx >= targets.size())) {
      MstUtils::error("supplied FASST solution is pointing to an out-of-range target", "FASST::getMatchStructures");
    }
    solsFromTarget[idx].push_back(i);
  }

  // flatten query into a single array of atoms
  AtomPointerVector queryAtoms;
  for (int k = 0; k < query.size(); k++) {
    for (int ai = 0; ai < query[k].size(); ai++) queryAtoms.push_back(query[k][ai]);
  }
  AtomPointerVector matchAtoms; matchAtoms.reserve(queryAtoms.size());

  // visit each target
  Structure dummy; RMSDCalculator rc;
  matches.resize(sols.size(), Structure());
  for (auto it = solsFromTarget.begin(); it != solsFromTarget.end(); ++it) {
    int idx = it->first;
    Structure* targetStruct = targetStructs[idx];
    AtomPointerVector& target = targets[idx];
    bool reread = detailed && targetSource[idx].memSave; // should we re-read the target structure?
    if (reread) {
      dummy.reset();
      // re-read structure
      if (targetSource[idx].type == targetFileType::PDB) {
        dummy.readPDB(targetSource[idx].file, "QUIET");
      } else if (targetSource[idx].type == targetFileType::BINDATABASE) {
        fstream ifs; MstUtils::openFile(ifs, targetSource[idx].file, fstream::in | fstream::binary, "FASST::getMatchStructures");
        ifs.seekg(targetSource[idx].loc);
        dummy.readData(ifs);
        ifs.close();
      } else if (targetSource[idx].type == targetFileType::STRUCTURE) {
        MstUtils::error("cannot produce a detailed match if target was initialized from object", "FASST::getMatchStructures");
      } else {
        MstUtils::error("don't know how to re-read target of this type", "FASST::getMatchStructures");
      }
      tr[idx].apply(dummy);
      targetStruct = &dummy;
    }

    // visit each solution from this target
    vector<int>& solIndices = it->second;
    for (int i = 0; i < solIndices.size(); i++) {
      int solIndex = solIndices[i];
      const fasstSolution& sol = sols[solIndex];
      Structure& match = matches[solIndex];
      vector<int> alignment = sol.getAlignment();
      if (alignment.size() != query.size()) {
        MstUtils::error("solution alignment size inconsistent with number of query segments", "FASST::getMatchStructures");
      }

      // excise match atoms that superimpose onto the query, in the right order
      matchAtoms.resize(0);
      for (int k = 0; k < alignment.size(); k++) {
        // copy matching residues from the original target structure
        int si = resToAtomIdx(alignment[k]);
        int L = query[k].size();
        if (si + L > target.size()) {
          MstUtils::error("solution points to atom outside of target range", "FASST::getMatchStructures");
        }
        for (int ai = si; ai < si + L; ai++) matchAtoms.push_back(target[ai]);
      }

      // cut out the part of the target Structure that will constitute the returned match
      switch(type) {
        case matchType::REGION: {
          for (int k = 0; k < alignment.size(); k++) {
            int si = resToAtomIdx(alignment[k]);
            int L = query[k].size();
            for (int ri = atomToResIdx(si); ri < atomToResIdx(si + L); ri++) {
              Residue* res = target[resToAtomIdx(ri)]->getResidue();
              if (reread) res = &(targetStruct->getResidue(res->getResidueIndex()));
              match.addResidue(res);
            }
          }
          break;
        }
        case matchType::WITHGAPS: {
          // figure out the range of residues to excise from target structure
          vector<bool> toInclude(targetStruct->residueSize());
          for (int i = 0; i < query.size(); i++) {
            // each individual segments should be included
            for (int k = alignment[i]; k < alignment[i] + atomToResIdx(query[i].size()); k++) toInclude[k] = true;
            // then some gaps also
            for (int j = 0; j < query.size(); j++) {
              if (i == j) continue;
              // if gap constrained, fill in between these two segments
              if (gapConstrained(i, j)) {
                if (alignment[i] > alignment[j]) MstUtils::error("solution not consistent with current gap constraints", "FASST::getMatchStructures");
                for (int k = alignment[i] + atomToResIdx(query[i].size()); k < alignment[j]; k++) {
                  toInclude[k] = true;
                }
              }
            }
          }
          for (int ri = 0; ri < toInclude.size(); ri++) {
            if (toInclude[ri]) match.addResidue(&(targetStruct->getResidue(ri)));
          }
          break;
        }
        case matchType::FULL:
          match = *targetStruct;
          break;
        default:
          MstUtils::error("unknown match output type", "FASST::getMatchStructures");
      }

      // align matching region onto query, transforming the match itself
      rc.align(matchAtoms, queryAtoms, match);
    }
  }
}

string FASST::toString(const fasstSolution& sol) {
  stringstream ss;
  ss << std::setprecision(6) << std::fixed << sol.getRMSD() << " " << targetStructs[sol.getTargetIndex()]->getName() << " [" << MstUtils::vecToString(sol.getAlignment(), ", ") << "]";
  return ss.str();
}
