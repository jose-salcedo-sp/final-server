use clap::Parser;
use std::path::PathBuf;

mod config;
mod proxy;

use proxy::ReverseProxy;

#[derive(Parser, Debug)]
#[command(name = "Chat Load Balancer")]
struct Args {
    /// Config path
    #[arg(long, short)]
    config_path: PathBuf,
}

#[tokio::main]
async fn main() -> std::io::Result<()> {
    let args = Args::parse();
    let proxy = ReverseProxy::from_config_file(&args.config_path);

    proxy.run().await?;

    return Ok(());
}
