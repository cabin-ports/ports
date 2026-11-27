// Scores, locally optimizes and docks a three-atom ligand (O-H donor and a
// carbon) near a four-atom receptor fragment (a carbonyl oxygen and three
// carbons), all written for this test.  The expected energies were measured
// with this port; the receptor is read from a file, so it is written to the
// probe's output directory first.
#include <vina.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

// One PDBQT ATOM record: coordinates in columns 31-54, AutoDock type in 78-79.
std::string pdbqt_atom(int serial, const char* name, double x, double y,
                       double z, const char* type) {
  char line[96];
  std::snprintf(line, sizeof line,
                "ATOM  %5d %-4s UNL     1    %8.3f%8.3f%8.3f  1.00  0.00    "
                "%6.3f %-2s\n",
                serial, name, x, y, z, 0.0, type);
  return line;
}

bool close_to(double actual, double expected, double tolerance) {
  return std::fabs(actual - expected) <= tolerance;
}

int fail(const char* what) {
  std::fprintf(stderr, "FAIL: %s\n", what);
  return 1;
}

}  // namespace

int main() {
  const char* dir = std::getenv("CABIN_PROFILE_DIR");
  if (dir == NULL) return fail("CABIN_PROFILE_DIR is unset");
  const std::string receptor =
      std::string(dir) + "/port-probe-autodock-vina-receptor.pdbqt";
  {
    std::ofstream out(receptor.c_str());
    out << pdbqt_atom(1, "C1", 0.0, 0.0, 0.0, "C")
        << pdbqt_atom(2, "O1", 1.23, 0.0, 0.0, "OA")
        << pdbqt_atom(3, "C2", -0.75, 1.30, 0.0, "C")
        << pdbqt_atom(4, "C3", -0.75, -1.30, 0.0, "C");
    if (!out) return fail("writing the receptor");
  }
  const std::string ligand = "ROOT\n" +
                             pdbqt_atom(1, "O", 4.0, 0.0, 0.0, "OA") +
                             pdbqt_atom(2, "H", 3.05, 0.0, 0.0, "HD") +
                             pdbqt_atom(3, "C", 4.48, 1.35, 0.0, "C") +
                             "ENDROOT\nTORSDOF 0\n";

  // One thread and a fixed seed keep the search reproducible.
  Vina v("vina", 1, 42, 0);
  v.set_receptor(receptor);
  std::remove(receptor.c_str());
  v.set_ligand_from_string(ligand);

  // The box is centred on the ligand's atoms, each side
  // ceil(2 * (largest offset from the centre + 4)) long.
  const std::vector<double> box = v.grid_dimensions_from_ligand(4.0);
  if (box.size() != 6 || !close_to(box[0], 11.53 / 3, 1e-9) ||
      !close_to(box[1], 0.45, 1e-9) || !close_to(box[2], 0.0, 1e-9) ||
      box[3] != 10 || box[4] != 10 || box[5] != 8)
    return fail("grid_dimensions_from_ligand");
  v.compute_vina_maps(box[0], box[1], box[2], box[3], box[4], box[5], 0.375);

  // A rigid ligand has no internal or torsional terms: the total is the
  // ligand-receptor energy.
  const std::vector<double> score = v.score();
  if (score.size() != 8 || !close_to(score[0], -0.267406, 1e-4) ||
      score[1] != score[0])
    return fail("score");
  for (size_t i = 2; i < score.size(); ++i)
    if (score[i] != 0) return fail("score terms");

  const std::vector<double> local = v.optimize();
  if (local.size() != 8 || !close_to(local[0], -0.277328, 1e-3))
    return fail("optimize");

  v.global_search(4, 3);
  const std::vector<std::vector<double> > poses = v.get_poses_energies(3, 3.0);
  if (poses.empty() || poses.size() > 3 ||
      !close_to(poses[0][0], -0.406, 5e-3))
    return fail("global_search");
  for (size_t i = 1; i < poses.size(); ++i)
    if (poses[i][0] < poses[i - 1][0]) return fail("pose order");
  const std::string pose_head = "MODEL 1\nREMARK VINA RESULT:";
  if (v.get_poses(1, 3.0).compare(0, pose_head.size(), pose_head) != 0)
    return fail("get_poses");
  return 0;
}
