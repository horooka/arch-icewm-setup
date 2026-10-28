#include "xtemplate/cli.hpp"
#include "xtemplate/xtemplate.hpp"

int main(int argc, char *argv[]) {
    // GUI mode when invoked without arguments; CLI mode otherwise so the GUI
    // binary stays usable in scripts.
    if (argc == 1)
        return run(argc, argv);
    return run_cli(argc, argv);
}
