# This file is part of the QuantumGate project. For copyright and
# licensing information refer to the license file(s) in the project root.

# This PowerShell script copies files from the Build folder to the Dist folder.
# Debug and Release builds have to be created first before running this script.
# All files from the Dist folder are deleted before copying begins.

param([string]$root_folder)

Function ExitWithError
{
	Param ([String]$error_string) 

	Write-Host "$error_string" -ForegroundColor red
	Write-Host("")
	exit 1
}

if ($root_folder.Length -eq 0)
{
	ExitWithError("Please specify the path to the QuantumGate project root folder.")
}

$build_folder = $root_folder + "\Build"
$dist_folder = $root_folder + "\Dist"

if (![System.IO.Directory]::Exists($build_folder)) 
{
	ExitWithError("The 'Build' folder does not exist ($build_folder). Has the project been built?")
}

if (![System.IO.Directory]::Exists($dist_folder)) 
{
	Write-Host("Creating 'Dist' folder ($dist_folder)...")
	Write-Host("")

	if (![System.IO.Directory]::CreateDirectory($dist_folder))
	{
		ExitWithError("Failed to create 'Dist' folder ($dist_folder).")
	}
}
else
{
	Write-Host("Cleaning 'Dist' folder ($dist_folder)...")
	Write-Host("")

	Get-ChildItem -Path $dist_folder | Remove-Item -Recurse -ErrorAction Stop
}

Function CreateDistFile
{
	Param ([String]$fname, [String]$sfolder, [String]$dfolder) 

	Return @{ "FileName" = $fname; "SourceFolder" = $sfolder; "DestinationFolder" = $dfolder; }
}

$dist_files = @(
	# Win32 Debug files
	CreateDistFile "CmdMess.exe" 								"Win32\Debug" "Win32\Debug"
	CreateDistFile "libcrypto32d.dll" 							"Win32\Debug" "Win32\Debug"
	CreateDistFile "libzstd32.dll" 								"Win32\Debug" "Win32\Debug"
	CreateDistFile "zlib32.dll" 								"Win32\Debug" "Win32\Debug"
	CreateDistFile "QuantumGate32D.dll" 						"Win32\Debug" "Win32\Debug"
	CreateDistFile "QuantumGate32D.lib" 						"Win32\Debug" "Win32\Debug"
	CreateDistFile "QuantumGateConsoleExample.exe" 				"Win32\Debug" "Win32\Debug"
	CreateDistFile "QuantumGateExtenderExample.exe" 			"Win32\Debug" "Win32\Debug"
	CreateDistFile "QuantumGateExtenderHandshakeExample.exe" 	"Win32\Debug" "Win32\Debug"
	CreateDistFile "QuantumGateExtenderModuleExample.dll"	 	"Win32\Debug" "Win32\Debug"
	CreateDistFile "QuantumGateStartupExample.exe" 				"Win32\Debug" "Win32\Debug"
	CreateDistFile "Socks5Extender32D.dll" 						"Win32\Debug" "Win32\Debug"
	CreateDistFile "TestApp32.exe" 								"Win32\Debug" "Win32\Debug"

	# Win32 Release files
	CreateDistFile "CmdMess.exe" 								"Win32\Release" "Win32\Release"
	CreateDistFile "libcrypto32.dll" 							"Win32\Release" "Win32\Release"
	CreateDistFile "libzstd32.dll" 								"Win32\Release" "Win32\Release"
	CreateDistFile "zlib32.dll" 								"Win32\Release" "Win32\Release"
	CreateDistFile "QuantumGate32.dll" 							"Win32\Release" "Win32\Release"
	CreateDistFile "QuantumGate32.lib" 							"Win32\Release" "Win32\Release"
	CreateDistFile "QuantumGateConsoleExample.exe" 				"Win32\Release" "Win32\Release"
	CreateDistFile "QuantumGateExtenderExample.exe" 			"Win32\Release" "Win32\Release"
	CreateDistFile "QuantumGateExtenderHandshakeExample.exe" 	"Win32\Release" "Win32\Release"
	CreateDistFile "QuantumGateExtenderModuleExample.dll"	 	"Win32\Release" "Win32\Release"
	CreateDistFile "QuantumGateStartupExample.exe" 				"Win32\Release" "Win32\Release"
	CreateDistFile "Socks5Extender32.dll" 						"Win32\Release" "Win32\Release"
	CreateDistFile "TestApp32.exe" 								"Win32\Release" "Win32\Release"	

	# x64 Debug files
	CreateDistFile "CmdMess.exe" 								"x64\Debug" "x64\Debug"
	CreateDistFile "libcrypto64d.dll" 							"x64\Debug" "x64\Debug"
	CreateDistFile "libzstd64.dll" 								"x64\Debug" "x64\Debug"
	CreateDistFile "zlib64.dll" 								"x64\Debug" "x64\Debug"
	CreateDistFile "QuantumGate64D.dll" 						"x64\Debug" "x64\Debug"
	CreateDistFile "QuantumGate64D.lib" 						"x64\Debug" "x64\Debug"
	CreateDistFile "QuantumGateConsoleExample.exe" 				"x64\Debug" "x64\Debug"
	CreateDistFile "QuantumGateExtenderExample.exe" 			"x64\Debug" "x64\Debug"
	CreateDistFile "QuantumGateExtenderHandshakeExample.exe" 	"x64\Debug" "x64\Debug"
	CreateDistFile "QuantumGateExtenderModuleExample.dll"	 	"x64\Debug" "x64\Debug"
	CreateDistFile "QuantumGateStartupExample.exe" 				"x64\Debug" "x64\Debug"
	CreateDistFile "Socks5Extender64D.dll" 						"x64\Debug" "x64\Debug"
	CreateDistFile "TestApp64.exe" 								"x64\Debug" "x64\Debug"

	# x64 Release files
	CreateDistFile "CmdMess.exe"								"x64\Release" "x64\Release"
	CreateDistFile "libcrypto64.dll"							"x64\Release" "x64\Release"
	CreateDistFile "libzstd64.dll"								"x64\Release" "x64\Release"
	CreateDistFile "zlib64.dll"									"x64\Release" "x64\Release"
	CreateDistFile "QuantumGate64.dll"							"x64\Release" "x64\Release"
	CreateDistFile "QuantumGate64.lib"							"x64\Release" "x64\Release"
	CreateDistFile "QuantumGateConsoleExample.exe"				"x64\Release" "x64\Release"
	CreateDistFile "QuantumGateExtenderExample.exe"				"x64\Release" "x64\Release"
	CreateDistFile "QuantumGateExtenderHandshakeExample.exe"	"x64\Release" "x64\Release"
	CreateDistFile "QuantumGateExtenderModuleExample.dll"		"x64\Release" "x64\Release"
	CreateDistFile "QuantumGateStartupExample.exe"				"x64\Release" "x64\Release"
	CreateDistFile "Socks5Extender64.dll"						"x64\Release" "x64\Release"
	CreateDistFile "TestApp64.exe"								"x64\Release" "x64\Release"
);

foreach ($file in $dist_files)
{
	$src_folder = -join($build_folder, "\", $file['SourceFolder']);
	$dest_folder = -join($dist_folder, "\", $file['DestinationFolder']);

	if (![System.IO.Directory]::Exists($dest_folder)) 
	{
		if (![System.IO.Directory]::CreateDirectory($dest_folder))
		{
			ExitWithError("Failed to create output folder ($dest_folder).")
		}
	}

	$src_file = -join($src_folder, "\", $file['FileName'])
	$dest_file = -join($dest_folder, "\", $file['FileName'])

	Write-Host("Copying file $src_file...")
	
	Copy-Item -Path $src_file -Destination $dest_file -Force

	if (!(Test-Path $dest_file))
	{
		ExitWithError("Failed to copy a file ($src_file).")
	}
}

Write-Host("")
Write-Host("All files have successfully been copied to the 'Dist' folder ($dist_folder).")
Write-Host("")

exit $LASTEXITCODE