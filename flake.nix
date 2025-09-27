{
  description = "A Nix-flake-based C/C++ development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    rust-overlay.url = "github:oxalica/rust-overlay";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    inputs:
    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      forEachSupportedSystem =
        f:
        inputs.nixpkgs.lib.genAttrs supportedSystems (
          system:
          let
            overlays = [ inputs.rust-overlay.overlays.default ];
            pkgs = import inputs.nixpkgs { inherit system overlays; };
          in
          f {
            inherit pkgs;
          }
        );
    in
    {
      devShells = forEachSupportedSystem (
        { pkgs }:
        {
          default =
            pkgs.mkShell.override
              {
                # Override stdenv in order to change compiler:
                # TODO Use 21
                stdenv = pkgs.llvmPackages_20.libcxxStdenv;
              }
              {
                packages =
                  with pkgs;
                  [
                    # C++
                    clang-tools
                    cmake
                    libcxx
                    #codespell
                    #conan
                    #cppcheck
                    #doxygen
                    #gtest
                    #lcov
                    #vcpkg
                    #vcpkg-tool

                    # Python
                    pkgs.uv

                    # Rust
                    (rust-bin.selectLatestNightlyWith (toolchain: toolchain.default))
                  ]
                  ++ (if system == "aarch64-darwin" then [ ] else [ pkgs.lldb ]);
              };
        }
      );
    };
}
