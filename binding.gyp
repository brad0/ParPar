{
  "targets": [
    {
      "target_name": "parpar_gf",
      "dependencies": ["gf-complete"],
      "sources": ["gf.cc", "md5/md5.c", "md5/md5-simd.c"],
      "include_dirs": ["gf-complete"],
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"OpenMP": "true"}}
        }, {
          "cflags": ["-march=native", "-O3", "-Wall"],
          "cxxflags": ["-march=native", "-O3", "-Wall", "-fopenmp"],
          "ldflags": ["-fopenmp"]
        }],
        ['OS=="mac"', {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-march=native", "-O3", "-Wall"],
            "OTHER_CPPFLAGS": ["-march=native", "-O3", "-Wall", "-fopenmp"],
            "OTHER_LDFLAGS": ["-fopenmp"]
          }
        }]
      ]
    },
    {
      "target_name": "gf-complete",
      "type": "static_library",
      "sources": [
        "gf-complete/gf.c",
        "gf-complete/gf_w16.c"
      ],
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2"}}
        }, {
          "cflags": ["-march=native","-Wall","-O3","-Wno-unused-function"],
          "ldflags": []
        }],
        ['OS=="mac"', {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-march=native","-Wall","-O3","-Wno-unused-function"],
            "OTHER_LDFLAGS": []
          }
        }],
        ['OS=="win" and target_arch=="x64"', {
          "sources": ["gf-complete/gf_w16_xor_jit_stub_masm64.asm"]
        }]
      ]
    }
  ]
}