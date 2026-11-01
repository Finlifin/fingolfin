//! Fingolfin compiler front end. Source spans are UTF-8 byte offsets.
pub mod abi;
pub mod compiler;
pub mod pattern;
pub mod resolve;
pub mod sdk;
pub mod sema;
pub mod source;
pub mod syntax;
pub mod types;
pub mod util;

pub mod eval;

pub mod ir;

pub mod codegen;

pub mod build;
pub mod package;

pub mod gate;

pub mod intrinsic;
pub mod runtime;
pub mod state;

#[cfg(feature = "ubpf")]
pub mod vm;

pub mod frame;

pub mod frame_schema;
