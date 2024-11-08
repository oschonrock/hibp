#include <functional>
#include <ostream>

// prefer use of std::function (ie stdlib type erasure) rather than templates to keep .hpp interface
// clean
using write_fn_t = std::function<void(const std::string&)>;

std::size_t get_last_prefix(const std::string& filename);

extern std::size_t start_prefix; // NOLINT non-cost-gobal
extern std::size_t next_prefix;  // NOLINT non-cost-gobal

void run_threads_text(std::ostream& output_db_stream);
void run_threads_ff(std::ostream& output_db_stream);

