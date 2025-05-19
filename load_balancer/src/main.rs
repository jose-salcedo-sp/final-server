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

    match &*proxy {
        ReverseProxy::Fl(fl) => {
            println!("ℹ️ Running FE <-> LOGIC load balancer");
            fl.clone().run().await?;
        },
        ReverseProxy::Ld(ld) => {
            println!("ℹ️ Running LOGIC <-> DATA load balancer");
            ld.clone().run().await?;
        },
    }

    return Ok(());
}
