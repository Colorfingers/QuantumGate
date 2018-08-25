![QuantumGate](https://github.com/kareldonk/QuantumGate/blob/master/Graphics/Docs/splash.jpg)

## About

QuantumGate is a peer-to-peer (P2P) communications protocol, library and API. The long-term goal for QuantumGate is to become a platform for distributed computing based on a mesh networking model. In the short term, the goal is to provide developers with networking technology that they can easily integrate and use in their own applications. [Click here](https://github.com/kareldonk/QuantumGate/wiki/QuantumGate-Overview) for a more detailed overview.

## Disclaimer

This software is currently in alpha development stage. **It has not yet undergone peer review and no security audits have yet been done on the cryptographic protocol and its implementation.** The API may also change, and while big changes are unlikely it will receive small updates as development progresses. Feel free to use and experiment with this software in your own projects, but keep the above information in mind.

## Status

Based on the architecture described in the [overview](https://github.com/kareldonk/QuantumGate/wiki/QuantumGate-Overview), the following main features are not yet implemented:

- DHT functionality
- Database functionality

## Platforms

QuantumGate is developed in C++17 and currently only supports the Microsoft Windows (x86/x64) platform. Support for Linux is planned for the future.

## Build

You'll require the latest version of Microsoft Visual Studio 2017, as well as the dependencies listed below. When the paths to the dependency includes and libraries have been configured properly, building is as simple as opening the `QuantumGate.sln` file in the project root with Visual Studio and issuing the build command for the entire solution.

### Dependencies

#### QuantumGate Core Library

| Name | Version |
|------|---------|
| [Microsoft Guideline Support Library](https://github.com/Microsoft/GSL) | Latest. |
| [OpenSSL](https://github.com/openssl/openssl) | At least 1.1.1 |
| [PCG-Random](https://github.com/imneme/pcg-cpp) | Latest. |
| [zlib](https://github.com/madler/zlib) | At least 1.2.11 |
| [Zstandard](https://github.com/facebook/zstd) | At least 1.3.4 |

#### QuantumGate Test Applications/Extenders

| Name | Version |
|------|---------|
| [Monocypher](https://github.com/LoupVaillant/Monocypher) | Latest. |
| [JSON for Modern C++](https://github.com/nlohmann/json) | At least 3.2.1 |

#### Naming

The QuantumGate MSVC project is configured to look for specific naming of the OpenSSL, zlib and Zstandard `.dll` and `.lib` files, depending on the platform (32 or 64 bit) you're building for. You may have to build these libraries yourself to specify the names, or, alternatively you may change the names in the source code and configuration to the ones you use.

| Library | x86 Debug | x86 Release | x64 Debug | x64 Release |
|---------|-----------|-------------|-----------|-------------|
| OpenSSL | libcrypto32d.lib, libcrypto32d.dll | libcrypto32.lib, libcrypto32.dll | libcrypto64d.lib, libcrypto64d.dll | libcrypto64.lib, libcrypto64.dll |
| zlib | zlib32.lib, zlib32.dll | zlib32.lib, zlib32.dll | zlib64.lib, zlib64.dll | zlib64.lib, zlib64.dll |
| Zstandard | zstd32.lib, zstd32.dll | zstd32.lib, zstd32.dll | zstd64.lib, zstd64.dll | zstd64.lib, zstd64.dll |

### Documentation

Documentation can be found in the [Wiki](https://github.com/kareldonk/QuantumGate/wiki).

### License

The license for the QuantumGate source code can be found in the [`LICENSE`](https://github.com/kareldonk/QuantumGate/blob/master/LICENSE) file in the project root. In addition, QuantumGate uses third party source code covered under separate licenses. This includes an implementation of SipHash (in `QuantumGateCryptoLib\SipHash`). Refer to the `LICENSE` files in the subfolders for details. Licenses for implementations of NTRUPrime (`QuantumGateCryptoLib\NTRUPrime`), NewHope (`QuantumGateCryptoLib\NewHope`) and Classic McEliece (`QuantumGateCryptoLib\McEliece`) have not yet been obtained.  

### Contact

For maximum efficiency and transparency [open an issue](https://github.com/kareldonk/QuantumGate/issues) on QuantumGate's GitHub repository with any questions and/or comments that you may have. Contact information for the author can be found at https://www.kareldonk.com.

### Donations

This project is self-funded. If you like it and would like to ensure its continued and speedy development, consider donating to the author. If this software has served you well (in commercial projects), consider supporting its further development through donations to the author. For details please contact the author using the above contact information. Please be advised, though, that **[the author does not accept contributions from governments, governmental organizations, government funded organizations, or other comparable terrorist organizations](https://blog.kareldonk.com/why-i-will-never-work-for-any-government-ever-again/)**. The author values the truth and will make no compromises with regard to the software, its mission and purpose.
