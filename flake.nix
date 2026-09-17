{
  description = "trogue 开发环境（raylib 6.0 + nlohmann/json + tl::expected + CMake/Ninja/GCC）";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { nixpkgs, ... }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      eachSystem = f: nixpkgs.lib.genAttrs systems (system: f (import nixpkgs { inherit system; }));
    in
    {
      devShells = eachSystem (pkgs: {
        default = pkgs.mkShell {
          packages = with pkgs; [
            # 构建工具链（本机无系统 gcc/cmake/pkg-config，全由 nix 提供）
            cmake
            ninja
            pkg-config
            # 引擎依赖；engine/CMakeLists.txt 用 find_package + pkg-config 回退查找
            raylib
            nlohmann_json
            tl-expected
            # tools/ 与 pixellab/ 的无头资产工具与单测（ctest 内调 python3）；
            # pixellab 转换层依赖 Pillow，故用带包的解释器
            (python3.withPackages (ps: [ ps.pillow ]))
            # 非 NixOS 上运行游戏所需：nixpkgs 的 libglvnd 把 GLX 厂商库搜索路径
            # 硬编码为 NixOS 专有的 /run/opengl-driver/lib，本机不存在，会导致
            # GLFW 报 "No GLXFBConfigs"。用 mesa 提供 libGLX_mesa.so.0 与 DRI 驱动，
            # 并按下述两个变量把它喂给 glvnd。
            mesa
          ];

          # glvnd 以裸文件名 dlopen("libGLX_mesa.so.0")，而 LD_LIBRARY_PATH 优先于
          # 其内嵌 RPATH，故指向 mesa 的 lib 即可命中厂商库
          LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath [ pkgs.mesa ];
          LIBGL_DRIVERS_PATH = "${pkgs.mesa}/lib/dri";

          # find_package(tl-expected) 无 pkg-config 回退路径，需显式给出 cmake 包目录；
          # raylib/nlohmann_json 也一并入路径，避免依赖 setup hook 的传播细节。
          CMAKE_PREFIX_PATH = pkgs.lib.makeSearchPath "lib/cmake" [
            pkgs.raylib
            pkgs.nlohmann_json
            pkgs.tl-expected
          ];

          shellHook = ''
            echo "trogue devShell: $(cmake --version | head -1) / $(g++ --version | head -1)"
            echo "  构建: cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug && cmake --build build"
            echo "  运行: ./build/bin/trogue（须在项目根，资产按 CWD assets/ 约定读取）"
            echo "  单测: ctest --test-dir build --output-on-failure"
          '';
        };
      });
    };
}
