#include "mstoptim.h"

using namespace MST;

double Optim::fminsearch(optimizerEvaluator& E, int numIters, vector<double>& solution, bool verbose) {
  double tol = 10E-6;
  Matrix x0(E.guessPoint()); // row vector
  int n = x0.length();
  Matrix simplex(n + 1, n);
  Matrix values(n + 1, 1);
  vector<int> order(n + 1);
  for (int i = 0; i < order.size(); i++) order[i] = i;

  // construct the initial simples
  for (int i = 0; i < simplex.size(); i++) {
    // the i-th point in the simplex is obtained by adjusting the i-th coordinate of the guess point
    simplex.row(i) = x0;
    if (i < n) {
      if (simplex(i, i) == 0) {
        simplex(i, i) = 0.00025; // default value
      } else {
        simplex(i, i) = 1.05 * simplex(i, i); // add 5%
      }
    }
    values(i) = E.eval(simplex.row(i));
  }

  // iterate until convergence of limit of iterations
  for (int k = 0; k < numIters; k++) {
    // sort points in the current simplex by function value
    sort(order.begin(), order.end(), [&values] (int i1, int i2) {return values(i1) < values(i2);});
    int b = order[0];   // best simplex
    int w = order[n-1]; // worst simplex to be kept
    int o = order[n];   // the very worst simplex (will be replaced)

    if (verbose) {
      cout << "iteration " << k+1 << ", the current " << order.size() << " simplex points:" << endl;
      for (int i = 0; i < order.size(); i++) {
        cout << simplex.row(order[i]) << " -> " << values(order[i]) << endl;
      }
    }

    // center of the current simplex
    Matrix m = simplex.mean(1);

    // quit if the current simplex is too small (stopping criterion by Dennis and Woods (1987))
    // bool tooSmall = true;
    // real d = max(1.0, simplex.row(0).norm());
    // for (int i = 1; i < simplex.size(); i++) {
    //   if ((simplex.row(0) - simplex.row(i)).norm() / d > 10E-4) { tooSmall = false; break; }
    // }
    // if (tooSmall) break;

    // quit if the current simplex is too small
    bool tooSmall = true;
    for (int i = 0; i < simplex.size(); i++) {
      if ((m - simplex.row(i)).norm() > tol) { tooSmall = false; break; }
    }
    if (tooSmall) break;

    // calculate the reflected point
    Matrix r = 2.0*m - simplex.row(o);
    double fr = E.eval(r);
    if ((values(b) <= fr) && (fr < values(w))) {
      // accept the reflection
      simplex.row(o) = r;
      values(o) = fr;
      if (verbose) cout << "Reflect\n";
      continue;
    } else if (fr < values(b)) {
      // calculate the expansion point
      Matrix s = 2*(m - simplex.row(o)) + m;
      double fs = E.eval(s);
      if (fs < fr) {
        // accept the expansion point
        simplex.row(o) = s;
        values(o) = fs;
        if (verbose) cout << "Expand\n";
        continue;
      } else {
        // accept the reflection
        simplex.row(o) = r;
        values(o) = fr;
        if (verbose) cout << "Reflect\n";
        continue;
      }
    } else if (fr >= values(w)) {
      // perform a contraction
      if (fr < values(o)) {
        Matrix c = 0.5*(r - m) + m;
        double fc = E.eval(c);
        if (fc < fr) {
          // accept contraction point
          simplex.row(o) = c;
          values(o) = fc;
          if (verbose) cout << "Contract outside\n";
          continue;
        }
      } else {
        Matrix cc = 0.5*(simplex.row(o) - m) + m;
        double fcc = E.eval(cc);
        if (fcc < values(o)) {
          // accept contraction point
          simplex.row(o) = cc;
          values(o) = fcc;
          if (verbose) cout << "Contract inside\n";
          continue;
        }
      }
    }
    // if nothing else worked, shrink
    for (int j = 1; j < order.size(); j++) {
      int i = order[j];
      simplex.row(i) = 0.5*(simplex.row(i) - simplex.row(b)) + simplex.row(b);
      values(i) = E.eval(simplex.row(i));
    }
    if (verbose) cout << "Shrink\n";
  }

  // find best solution
  int bestPoint;
  double bestScore = MstUtils::min((vector<real>) values, -1, -1, &bestPoint);
  solution = simplex.row(bestPoint);
  return bestScore;
}
