// #include "absl/log/globals.h"
// #include "absl/log/initialize.h"
// #include "absl/log/log.h"
// #include "nyla/shipgame/circle.h"
//
// namespace nyla {
//
// int main() {
//   absl::InitializeLog();
//   absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
//
//   for (const auto& vert : TriangulateCircle(0)) {
//     LOG(INFO) << vert;
//   }
//
//   LOG(INFO) << "==========================";
//
//   for (const auto& vert : TriangulateCircle(10)) {
//     LOG(INFO) << vert;
//   }
//
//   return 0;
// }
//
// }  // namespace nyla
//
// int main() { return nyla::main(); }
