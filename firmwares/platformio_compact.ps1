param(
    [Parameter(Mandatory = $true)]
    [string]$PlatformIo,

    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    [Parameter(Mandatory = $true)]
    [string]$EnvironmentName,

    [Parameter(Mandatory = $true)]
    [ValidateSet("build", "erase", "upload")]
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
    Write-Host ("`r    Gravando: [{0}] {1,3}%" -f $bar, $boundedPercent) -NoNewline
    $script:progressActive = $true
}

try {
    $env:PYTHONUNBUFFERED = "1"

    if ($Action -eq "build") {
        Write-CompactLine "    Preparando compilacao..."
    } else {
        Write-CompactLine "    Preparando comunicacao serial..."
    }

    & $PlatformIo @pioArguments 2>&1 | ForEach-Object {
        $line = [string]$_
        $cleanLine = ($line -replace $ansiPattern, "").TrimEnd("`r", "`n")
        $capturedOutput.Add($cleanLine) | Out-Null

        if ($Action -eq "build") {
            if (-not $buildCompileShown -and $cleanLine -match "^Compiling ") {
                Write-CompactLine "    Compilando arquivos..."
                $buildCompileShown = $true
            } elseif (-not $buildLinkShown -and $cleanLine -match "^Linking ") {
                Write-CompactLine "    Montando o firmware..."
                $buildLinkShown = $true
            } elseif (-not $buildPackageShown -and $cleanLine -match "^Building .*firmware\.bin") {
                Write-CompactLine "    Gerando a imagem final..."
                $buildPackageShown = $true
            }

            return
        }

        if (-not $connectionShown -and $cleanLine -match "^Connecting") {
            Write-CompactLine "    Conectando ao ESP32..."
            $connectionShown = $true
            return
        }

        if (-not $chipShown -and $cleanLine -match "^Chip is (.+)$") {
            Write-CompactLine ("    Chip detectado: " + $Matches[1])
            $chipShown = $true
            return
        }

        if ($Action -eq "erase") {
            if (-not $eraseShown -and $cleanLine -match "^Erasing flash") {
                Write-CompactLine "    Apagando a flash completa..."
                $eraseShown = $true
            } elseif (-not $eraseCompletedShown -and $cleanLine -match "^Chip erase completed successfully in (.+)$") {
                Write-CompactLine ("    Flash apagada com sucesso em " + $Matches[1])
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
            Write-CompactLine "    Gravacao verificada."
            $verificationShown = $true
            $trackApplicationWrite = $false
            return
        }

        if (-not $resetShown -and $cleanLine -match "^Hard resetting") {
            Write-CompactLine "    Reiniciando o ESP32..."
            $resetShown = $true
        }
    }

    $processExitCode = if ($null -eq $LASTEXITCODE) { 1 } else { [int]$LASTEXITCODE }
} catch {
    Complete-ProgressLine
    Write-Host ("    Erro ao iniciar o PlatformIO: " + $_.Exception.Message)
    exit 1
}

Complete-ProgressLine

if ($processExitCode -ne 0) {
    Write-Host ""
    Write-Host "    Detalhes do erro:"
    $serialOpenFailure = @($capturedOutput | Where-Object { $_ -match "(?i)Could not open COM[0-9]+" })

    if ($serialOpenFailure.Count -gt 0) {
        Write-Host ("      Nao foi possivel abrir " + $Port + ". A porta nao existe ou esta ocupada.")
        Write-Host "      Confira o cabo USB e feche programas que estejam usando a serial."
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
