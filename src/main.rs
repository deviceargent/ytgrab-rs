use std::process::ExitCode;

use clap::Parser;
use tracing_subscriber::EnvFilter;
use ytgrab::cli::Cli;
use ytgrab::runner;

fn main() -> ExitCode {
    let cli = Cli::parse();

    let level = if cli.verbose {
        "debug"
    } else if cli.quiet {
        "error"
    } else {
        "info"
    };
    let _ = tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::new(format!("ytgrab={level},warn")))
        .with_writer(std::io::stderr)
        .try_init();

    let runtime = match tokio::runtime::Builder::new_multi_thread().enable_all().build() {
        Ok(rt) => rt,
        Err(e) => {
            eprintln!("error: could not start the async runtime: {e}");
            return ExitCode::FAILURE;
        }
    };

    match runtime.block_on(runner::run(cli)) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::FAILURE
        }
    }
}
