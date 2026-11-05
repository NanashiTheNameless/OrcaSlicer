#include "BuildCommit.hpp"
#include "git_commit_hash.h"

namespace Slic3r { namespace GUI {

const char *const build_commit_hash  = GIT_COMMIT_HASH;
const char *const build_commit_label = GIT_COMMIT_HASH GIT_COMMIT_SUFFIX;
const long long    build_commit_time  = GIT_COMMIT_TIME;
const bool         build_is_nightly   = ORCA_NIGHTLY_BUILD != 0;

}} // namespace Slic3r::GUI
