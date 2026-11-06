param(
    [Parameter(Mandatory = $true)]
    [string]$PlatformIo,

    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    [Parameter(Mandatory = $true)]
    [string]$EnvironmentName,

    [Parameter(Mandatory = $true)]
    [ValidateSet(
        "build",
        "erase",
        "upload",
        "backup-logs",
        "erase-except-logs",
        "verify-logs-after-erase",
        "verify-logs-after-upload"
    )]
    [string]$Action,

    [string]$Port = ""
)

$pioArguments = @(
    "run",
    "--project-dir", $ProjectDir,
    "--environment", $EnvironmentName
)

if ($Action -eq "erase") {
    $pioArguments += @("--target", "erase", "--upload-port", $Port)
} elseif ($Action -eq "upload") {
    $pioArguments += @("--target", "upload", "--upload-port", $Port)
} elseif ($Action -eq "backup-logs") {
    $pioArguments += @("--target", "backup_logs", "--upload-port", $Port)
} elseif ($Action -eq "erase-except-logs") {
    $pioArguments += @("--target", "erase_except_logs", "--upload-port", $Port)
} elseif ($Action -eq "verify-logs-after-erase") {
    $pioArguments += @("--target", "verify_logs_after_erase", "--upload-port", $Port)
} elseif ($Action -eq "verify-logs-after-upload") {
    $pioArguments += @("--target", "verify_logs_after_upload", "--upload-port", $Port)
}

$capturedOutput = New-Object "System.Collections.Generic.List[string]"
$ansiPattern = "$([char]27)\[[0-9;?]*[ -/]*[@-~]"
$script:progressActive = $false
$buildCompileShown = $false
$buildLinkShown = $false
$buildPackageShown = $false
$connectionShown = $false
$chipShown = $false
$eraseShown = $false
$eraseCompletedShown = $false
$trackApplicationWrite = $false
$applicationProgressSeen = $false
$verificationShown = $false
$resetShown = $false
$deviceShown = $false
$flashSizeShown = $false
$layoutShown = $false

function Complete-ProgressLine {
    if ($script:progressActive) {
        Write-Host ""
        $script:progressActive = $false
    }
}

function Write-CompactLine([string]$Text) {
    Complete-ProgressLine
    Write-Host $Text
}

function Write-UploadProgress([int]$Percent) {
    $width = 30
    $boundedPercent = [Math]::Max(0, [Math]::Min(100, $Percent))
    $filled = [Math]::Floor($boundedPercent * $width / 100)
    $bar = ("#" * $filled) + ("-" * ($width - $filled))
    Write-Host ("`r    Writing: [{0}] {1,3}%" -f $bar, $boundedPercent) -NoNewline
    $script:progressActive = $true
}

try {
    $env:PYTHONUNBUFFERED = "1"

    if ($Action -eq "build") {
        Write-CompactLine "    Preparing build..."
    } elseif ($Action -eq "backup-logs") {
        Write-CompactLine "    Preparing forensic backup..."
    } elseif ($Action -eq "erase-except-logs") {
        Write-CompactLine "    Preparing selective erase..."
    } elseif ($Action -like "verify-logs-*") {
        Write-CompactLine "    Preparing binary verification..."
    } else {
        Write-CompactLine "    Preparing serial communication..."
    }

    & $PlatformIo @pioArguments 2>&1 | ForEach-Object {
        $line = [string]$_
        $cleanLine = ($line -replace $ansiPattern, "").TrimEnd("`r", "`n")
        $capturedOutput.Add($cleanLine) | Out-Null

        if ($cleanLine -eq "LOG_LAYOUT_OK=0x310000:0x0F0000:0x400000") {
            if (-not $layoutShown) {
                Write-CompactLine "    Layout confirmed: logs at 0x310000-0x3FFFFF (960 KiB)."
                $layoutShown = $true
            }
            return
        }

        if ($cleanLine -match "^LOG_DEVICE_OK=(.+)$") {
            if (-not $deviceShown) {
                Write-CompactLine ("    ESP32 confirmed: " + $Matches[1])
                $deviceShown = $true
            }
            return
        }

        if ($cleanLine -match "^LOG_FLASH_SIZE_OK=(.+)$") {
            if (-not $flashSizeShown) {
                Write-CompactLine ("    Flash confirmed: " + $Matches[1])
                $flashSizeShown = $true
            }
            return
        }

        if ($cleanLine -eq "LOG_BACKUP_STARTED") {
            Write-CompactLine "    Reading the entire logs partition..."
            return
        }

        if ($cleanLine -match "^LOG_BACKUP_SAVED=(.+)$") {
            Write-CompactLine ("    Backup saved to: " + $Matches[1])
            return
        }

        if ($cleanLine -match "^LOG_BACKUP_SHA256=([0-9a-fA-F]+)$") {
            Write-CompactLine ("    SHA256: " + $Matches[1].ToLowerInvariant())
            return
        }

        if ($cleanLine -eq "LOG_PRESERVE_ERASE_START") {
            Write-CompactLine "    Erasing from address 0x000000 to 0x30FFFF..."
            return
        }

        if ($cleanLine -eq "LOG_PRESERVE_ERASE_DONE") {
            Write-CompactLine "    Selective erase complete."
            return
        }

        if ($cleanLine -match "^LOG_VERIFY_STARTED=(after-erase|after-upload)$") {
            Write-CompactLine "    Rereading the 960 KiB of the logs partition..."
            return
        }

        if ($cleanLine -match "^LOG_VERIFY_OK=(after-erase|after-upload)$") {
            Write-CompactLine "    SHA256 and binary comparison confirmed."
            return
        }

        if ($cleanLine -match "^LOG_ERROR=(.+)$") {
            Write-CompactLine ("    ERROR: " + $Matches[1])
            return
        }

        if ($Action -eq "build") {
            if (-not $buildCompileShown -and $cleanLine -match "^Compiling ") {
                Write-CompactLine "    Compiling files..."
                $buildCompileShown = $true
            } elseif (-not $buildLinkShown -and $cleanLine -match "^Linking ") {
                Write-CompactLine "    Linking the firmware..."
                $buildLinkShown = $true
            } elseif (-not $buildPackageShown -and $cleanLine -match "^Building .*firmware\.bin") {
                Write-CompactLine "    Building the final image..."
                $buildPackageShown = $true
            }

            return
        }

        if (-not $connectionShown -and $cleanLine -match "^Connecting") {
            Write-CompactLine "    Connecting to the ESP32..."
            $connectionShown = $true
            return
        }

        if (-not $chipShown -and $cleanLine -match "^Chip is (.+)$") {
            Write-CompactLine ("    Chip detected: " + $Matches[1])
            $chipShown = $true
            return
        }

        if ($Action -eq "erase") {
            if (-not $eraseShown -and $cleanLine -match "^Erasing flash") {
                Write-CompactLine "    Erasing the entire flash..."
                $eraseShown = $true
            } elseif (-not $eraseCompletedShown -and $cleanLine -match "^Chip erase completed successfully in (.+)$") {
                Write-CompactLine ("    Flash erased successfully in " + $Matches[1])
                $eraseCompletedShown = $true
            }

            return
        }

        if ($cleanLine -match "^Compressed ([0-9]+) bytes") {
            $trackApplicationWrite = [int64]$Matches[1] -ge 100000
            return
        }

        if ($trackApplicationWrite -and $cleanLine -match "^Writing at .*\(\s*([0-9]+)\s*%\)") {
            Write-UploadProgress ([int]$Matches[1])
            $applicationProgressSeen = $true
            return
        }

        if ($trackApplicationWrite -and $applicationProgressSeen -and -not $verificationShown -and $cleanLine -match "^Hash of data verified") {
            Write-CompactLine "    Write verified."
            $verificationShown = $true
            $trackApplicationWrite = $false
            return
        }

        if (-not $resetShown -and $cleanLine -match "^Hard resetting") {
            Write-CompactLine "    Resetting the ESP32..."
            $resetShown = $true
        }
    }

    $processExitCode = if ($null -eq $LASTEXITCODE) { 1 } else { [int]$LASTEXITCODE }
} catch {
    Complete-ProgressLine
    Write-Host ("    Failed to start PlatformIO: " + $_.Exception.Message)
    exit 1
}

Complete-ProgressLine

if ($processExitCode -ne 0) {
    Write-Host ""
    Write-Host "    Error details:"
    $serialOpenFailure = @($capturedOutput | Where-Object { $_ -match "(?i)Could not open COM[0-9]+" })

    if ($serialOpenFailure.Count -gt 0) {
        Write-Host ("      Could not open " + $Port + ". The port does not exist or is in use.")
        Write-Host "      Check the USB cable and close programs that are using the serial port."
        exit ([int]$processExitCode)
    }

    $errorPattern = "(?i)(fatal|error|cannot|could not|unable|timed?\s*out|not found|does not exist|permission|denied|access is denied|undefined reference|no such file|traceback|exception|wrong boot mode|no serial data|hint:)"
    $contextIndexes = @()

    for ($index = 0; $index -lt $capturedOutput.Count; $index++) {
        if ($capturedOutput[$index] -match $errorPattern) {
            $firstContextIndex = [Math]::Max(0, $index - 2)
            $lastContextIndex = [Math]::Min($capturedOutput.Count - 1, $index + 3)
            $contextIndexes += $firstContextIndex..$lastContextIndex
        }
    }

    $errorLines = @(
        $contextIndexes |
            Sort-Object -Unique |
            ForEach-Object { $capturedOutput[$_] } |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and $_ -notmatch "^=+" }
    )

    if ($errorLines.Count -eq 0) {
        $errorLines = @($capturedOutput | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Last 15)
    }

    $previousLine = $null
    foreach ($errorLine in $errorLines) {
        if ($errorLine -ne $previousLine) {
            Write-Host ("      " + $errorLine.Trim())
            $previousLine = $errorLine
        }
    }
}

exit ([int]$processExitCode)
