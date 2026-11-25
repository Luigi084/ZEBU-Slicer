<#
  ZEBU Slicer - banc de mesure Orca / ZEBU (Windows, PowerShell 5.1 ou 7)

  Tranche chaque projet .3mf d'un dossier avec OrcaSlicer puis avec ZEBU Slicer, en ligne de
  commande (aucune fenetre), plusieurs fois, et note pour chaque passe :
    - le temps total (chargement + tranchage + export du G-code) ;
    - le temps processeur, d'ou le nombre moyen de threads reellement occupes ;
    - la memoire maximale ;
    - une empreinte du G-code sans ses commentaires, pour verifier que les deux logiciels
      sortent les memes mouvements (la base de comparaison pour l'export parallele de la 0.2.0).

  Utilisation (PowerShell, dans le dossier du script) :
    powershell -ExecutionPolicy Bypass -File .\zebu_bench.ps1 -Models D:\bench_models
    powershell -ExecutionPolicy Bypass -File .\zebu_bench.ps1 -Models D:\bench_models -Threads 4,8,16,32

  Les reglages viennent des projets eux-memes : enregistre chaque projet (Fichier > Enregistrer le
  projet) avec la machine, le process et le filament voulus. Les deux logiciels tranchent donc
  exactement la meme chose.
#>
param(
    [Parameter(Mandatory = $true)][string]$Models,
    [string]$Orca  = "",
    [string]$Zebu  = "",
    [int]$Runs     = 3,
    # 0 = tous les threads de la machine. Plusieurs valeurs separees par des virgules = courbe de montee
    # en charge. Texte, car "powershell -File" ne transmet pas les listes : "4,8,16,32" arriverait colle.
    [string]$Threads = "0",
    [string]$Out   = "",
    # Garde le G-code de la derniere passe de chaque mesure (dans le dossier des resultats, sous gcode\).
    [switch]$KeepGcode,
    [switch]$SkipOrca,
    [switch]$SkipZebu
)
$ErrorActionPreference = "Stop"
$ThreadList = @($Threads -split "[,; ]+" | Where-Object { $_ -ne "" } | ForEach-Object { [int]$_ })
if ($ThreadList.Count -eq 0) { $ThreadList = @(0) }

function Find-Exe([string]$given, [string[]]$candidates) {
    if ($given) {
        if (-not (Test-Path -LiteralPath $given)) { throw "Introuvable : $given" }
        return (Resolve-Path -LiteralPath $given).Path
    }
    foreach ($c in $candidates) { if (Test-Path -LiteralPath $c) { return $c } }
    return $null
}

# Empreinte du G-code sans les lignes de commentaire (en-tete, version, date, vignettes, temps estimes).
function Get-GcodeFingerprint([string]$dir) {
    $files = Get-ChildItem -LiteralPath $dir -Filter "*.gcode" | Sort-Object Name
    if (-not $files) { return "" }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $enc = New-Object System.Text.UTF8Encoding($false)
    $nl  = $enc.GetBytes("`n")
    foreach ($f in $files) {
        $reader = New-Object System.IO.StreamReader($f.FullName)
        try {
            while ($null -ne ($line = $reader.ReadLine())) {
                $t = $line.Trim()
                if ($t.Length -eq 0 -or $t.StartsWith(";")) { continue }
                $b = $enc.GetBytes($t)
                [void]$sha.TransformBlock($b, 0, $b.Length, $null, 0)
                [void]$sha.TransformBlock($nl, 0, $nl.Length, $null, 0)
            }
        } finally { $reader.Dispose() }
    }
    [void]$sha.TransformFinalBlock([byte[]]@(), 0, 0)
    return ([System.BitConverter]::ToString($sha.Hash) -replace "-", "").Substring(0, 16)
}

function Get-Median([double[]]$values) {
    $s = $values | Sort-Object
    $n = $s.Count
    if ($n -eq 0) { return 0 }
    if ($n % 2 -eq 1) { return $s[[int](($n - 1) / 2)] }
    return ($s[$n / 2 - 1] + $s[$n / 2]) / 2
}

# Codes de sortie de la ligne de commande d'Orca (src/libslic3r/Utils.hpp), les plus frequents.
function Get-CliError([int]$code) {
    if ($code -eq -50) { return "rien a trancher : plateau vide, ou aucun objet entierement dans le volume d'impression (un plateau vide dans le projet suffit)" }
    if ($code -eq -51) { return "reglages refuses par la verification avant tranchage (la meme verification que dans linterface)" }
    if ($code -eq -52) { return "un objet depasse en partie du plateau" }
    return ""
}

# Une passe : lance le slicer, mesure, verifie le G-code.
function Invoke-Slice([string]$exe, [string]$model, [string]$outDir) {
    if (Test-Path -LiteralPath $outDir) { Remove-Item -LiteralPath $outDir -Recurse -Force }
    New-Item -ItemType Directory -Path $outDir | Out-Null
    # --arrange 0 : garder le placement du projet (sinon la ligne de commande peut re-ranger les pieces).
    $arguments = @("--slice", "0", "--arrange", "0", "--allow-newer-file", "--outputdir", "`"$outDir`"", "`"$model`"")
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $errFile = Join-Path $outDir "stderr.txt"
    $outFile = Join-Path $outDir "stdout.txt"
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden `
        -RedirectStandardError $errFile -RedirectStandardOutput $outFile
    # Lire Handle tout de suite garde le processus ouvert : sinon ExitCode et TotalProcessorTime sont perdus a la fin.
    $null = $p.Handle
    $peak = 0L
    while (-not $p.HasExited) {
        try {
            $p.Refresh()
            if ($p.PeakWorkingSet64 -gt $peak) { $peak = $p.PeakWorkingSet64 }
        } catch { }
        Start-Sleep -Milliseconds 100
    }
    $p.WaitForExit()
    $sw.Stop()
    $cpu = 0.0
    try { $cpu = $p.TotalProcessorTime.TotalSeconds } catch { }
    $gcodes = @(Get-ChildItem -LiteralPath $outDir -Filter "*.gcode" -ErrorAction SilentlyContinue)
    # Un code de sortie non nul accompagne parfois un simple avertissement : le G-code fait foi, le code est note.
    $ok = $gcodes.Count -gt 0
    $message = ""
    if (-not $ok) {
        $lines = @()
        foreach ($f in @($errFile, $outFile)) {
            if (Test-Path -LiteralPath $f) { $lines += @(Get-Content -LiteralPath $f -ErrorAction SilentlyContinue | Where-Object { $_.Trim() -ne "" }) }
        }
        $message = ($lines | Select-Object -Last 8) -join " | "
    }
    return [pscustomobject]@{
        Message = $message
        Ok      = $ok
        Exit    = $p.ExitCode
        Wall    = $sw.Elapsed.TotalSeconds
        Cpu     = $cpu
        PeakMB  = [math]::Round($peak / 1MB)
        Gcodes  = $gcodes.Count
    }
}

# ---------------------------------------------------------------------------------------------
$orcaExe = $null; $zebuExe = $null
if (-not $SkipOrca) {
    $orcaExe = Find-Exe $Orca @("$env:ProgramFiles\OrcaSlicer\orca-slicer.exe", "$env:ProgramFiles\OrcaSlicer\OrcaSlicer.exe")
    if (-not $orcaExe) { throw "OrcaSlicer introuvable : indique-le avec -Orca `"C:\...\orca-slicer.exe`" (ou -SkipOrca)." }
}
if (-not $SkipZebu) {
    $zebuExe = Find-Exe $Zebu @("$env:ProgramFiles\ZEBU Slicer\zebu-slicer.exe")
    if (-not $zebuExe) { throw "ZEBU Slicer introuvable : indique-le avec -Zebu `"C:\...\zebu-slicer.exe`" (ou -SkipZebu)." }
}
$apps = @()
if ($orcaExe) { $apps += [pscustomobject]@{ Name = "Orca"; Exe = $orcaExe } }
if ($zebuExe) { $apps += [pscustomobject]@{ Name = "ZEBU"; Exe = $zebuExe } }
if ($apps.Count -eq 0) { throw "Rien a mesurer." }

$modelFiles = @(Get-ChildItem -LiteralPath $Models -Filter "*.3mf" | Sort-Object Name)
if ($modelFiles.Count -eq 0) { throw "Aucun .3mf dans $Models" }

# Un projet enregistre (Fichier > Enregistrer le projet) contient ses reglages dans Metadata/project_settings.config.
# Un 3MF "publie" ou "generique" n'en a pas : la ligne de commande prendrait alors une imprimante par defaut.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$noSettings = @()
foreach ($m in $modelFiles) {
    $zip = [System.IO.Compression.ZipFile]::OpenRead($m.FullName)
    try {
        if (-not ($zip.Entries | Where-Object { $_.FullName -eq "Metadata/project_settings.config" })) { $noSettings += $m.Name }
    } finally { $zip.Dispose() }
}
if ($noSettings.Count -gt 0) {
    Write-Warning ("Ces fichiers ne contiennent pas les reglages du projet : " + ($noSettings -join ", "))
    Write-Warning "Ouvre-les dans ZEBU et utilise Fichier > Enregistrer le projet (Ctrl+S), pas Publier ni Exporter."
    throw "Projets sans reglages."
}

if (-not $Out) { $Out = Join-Path (Get-Location) ("zebu_bench_" + (Get-Date -Format "yyyyMMdd_HHmmss")) }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$work = Join-Path $Out "work"

$cpuInfo = Get-CimInstance Win32_Processor
$logical = [Environment]::ProcessorCount
$ramGB   = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
$load    = ($cpuInfo | Measure-Object -Property LoadPercentage -Average).Average

$info = @()
$info += "Date        : " + (Get-Date -Format "yyyy-MM-dd HH:mm")
$info += "Processeur  : " + (($cpuInfo | Select-Object -First 1).Name.Trim()) + " ($logical threads)"
$info += "Memoire     : $ramGB Go"
$info += "Windows     : " + (Get-CimInstance Win32_OperatingSystem).Caption
foreach ($a in $apps) {
    $item = Get-Item -LiteralPath $a.Exe
    $ver = $item.VersionInfo.ProductVersion
    if (-not $ver) { $ver = $item.VersionInfo.FileVersion }
    if (-not $ver) { $ver = "version ?" }
    $info += ("{0,-11} : {1}, du {2:yyyy-MM-dd} ({3})" -f $a.Name, $ver, $item.LastWriteTime, $a.Exe)
}
$info += "Passes      : $Runs (+1 de chauffe par projet et par logiciel, non comptee)"
$info += "Threads     : " + (($ThreadList | ForEach-Object { if ($_ -le 0) { "tous" } else { "$_" } }) -join ", ")
$info | ForEach-Object { Write-Host $_ }
$info | Set-Content -Path (Join-Path $Out "machine.txt") -Encoding UTF8
if ($load -gt 15) {
    Write-Warning "Le processeur est deja occupe a $load % : ferme les autres logiciels pour des mesures fiables."
}

$rows = New-Object System.Collections.Generic.List[object]

try {
    foreach ($m in $modelFiles) {
        foreach ($a in $apps) {
            Write-Host ""
            Write-Host ("== {0} | {1} : chauffe..." -f $m.Name, $a.Name)
            $w = Invoke-Slice $a.Exe $m.FullName (Join-Path $work "warmup")
            if (-not $w.Ok) {
                Write-Warning ("{0} a echoue sur {1} (code {2}, {3} G-code) : projet ignore pour ce logiciel." -f $a.Name, $m.Name, $w.Exit, $w.Gcodes)
                $why = Get-CliError $w.Exit
                if ($why) { Write-Host ("   -> " + $why) }
                if ($w.Message) { Write-Host ("   -> message du slicer : " + $w.Message) }
                ("{0} | {1} | code {2} | {3} | {4}" -f $m.Name, $a.Name, $w.Exit, $why, $w.Message) | Add-Content -Path (Join-Path $Out "erreurs.txt") -Encoding UTF8
                continue
            }
            foreach ($t in $ThreadList) {
                $n = $t
                if ($n -le 0 -or $n -gt $logical) { $n = $logical }
                # Limiter les threads de l'exterieur (affinite) ne marche pas : le slicer passe outre. ZEBU les
                # limite lui-meme avec ZEBU_MAX_THREADS ; Orca ne sait pas le faire, il n'est mesure qu'avec tous.
                if ($n -lt $logical -and $a.Name -ne "ZEBU") {
                    Write-Host ("   {0,2} threads : non mesure pour {1} (il ne sait pas limiter ses threads)" -f $n, $a.Name)
                    continue
                }
                if ($n -lt $logical) { $env:ZEBU_MAX_THREADS = "$n" } else { Remove-Item Env:ZEBU_MAX_THREADS -ErrorAction SilentlyContinue }
                # ZEBU ecrit la duree de chaque etape dans ce fichier (une section par passe).
                $timingDir = Join-Path $Out "etapes"
                New-Item -ItemType Directory -Force -Path $timingDir | Out-Null
                $env:ZEBU_TIMINGS = Join-Path $timingDir ("{0}_{1}_{2}t.txt" -f $m.BaseName, $a.Name, $n)
                for ($r = 1; $r -le $Runs; $r++) {
                    $dir = Join-Path $work ("{0}_{1}_{2}t_{3}" -f $m.BaseName, $a.Name, $n, $r)
                    $res = Invoke-Slice $a.Exe $m.FullName $dir
                    $fp = ""
                    if ($res.Ok -and $r -eq $Runs) { $fp = Get-GcodeFingerprint $dir }
                    $avg = 0
                    if ($res.Wall -gt 0) { $avg = [math]::Round($res.Cpu / $res.Wall, 1) }
                    $rows.Add([pscustomobject]@{
                        Projet      = $m.Name
                        Logiciel    = $a.Name
                        Threads     = $n
                        Passe       = $r
                        Ok          = $res.Ok
                        Code        = $res.Exit
                        Temps_s     = [math]::Round($res.Wall, 2)
                        CPU_s       = [math]::Round($res.Cpu, 2)
                        Threads_moy = $avg
                        RAM_max_Mo  = $res.PeakMB
                        Empreinte   = $fp
                    })
                    $status = ""
                    if (-not $res.Ok) { $status = "  ECHEC (code $($res.Exit))" }
                    Write-Host ("   {0,2} threads, passe {1}/{2} : {3,7:N1} s, {4,5:N1} threads occupes en moyenne, {5} Mo{6}" -f `
                        $n, $r, $Runs, $res.Wall, $avg, $res.PeakMB, $status)
                    if ($r -lt $Runs) { Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue }
                    elseif ($KeepGcode -and $res.Ok) {
                        $keep = Join-Path (Join-Path $Out "gcode") ("{0}_{1}_{2}t" -f $m.BaseName, $a.Name, $n)
                        New-Item -ItemType Directory -Force -Path $keep | Out-Null
                        Get-ChildItem -LiteralPath $dir -Filter "*.gcode" | Copy-Item -Destination $keep
                    }
                }
                Remove-Item Env:ZEBU_TIMINGS -ErrorAction SilentlyContinue
                Remove-Item Env:ZEBU_MAX_THREADS -ErrorAction SilentlyContinue
            }
        }
    }
} finally {
    Remove-Item Env:ZEBU_TIMINGS -ErrorAction SilentlyContinue
    Remove-Item Env:ZEBU_MAX_THREADS -ErrorAction SilentlyContinue
}

$csv = Join-Path $Out "passes.csv"
$rows | Export-Csv -Path $csv -NoTypeInformation -Delimiter ";" -Encoding UTF8

# Resume : mediane par projet / logiciel / threads, puis gain de ZEBU sur Orca.
$summary = New-Object System.Collections.Generic.List[object]
$groups = $rows | Where-Object { $_.Ok } | Group-Object Projet, Logiciel, Threads
foreach ($g in $groups) {
    $first = $g.Group[0]
    $summary.Add([pscustomobject]@{
        Projet      = $first.Projet
        Logiciel    = $first.Logiciel
        Threads     = $first.Threads
        Mediane_s   = [math]::Round((Get-Median ([double[]]($g.Group | ForEach-Object { $_.Temps_s }))), 2)
        Threads_moy = [math]::Round(($g.Group | Measure-Object Threads_moy -Average).Average, 1)
        RAM_max_Mo  = ($g.Group | Measure-Object RAM_max_Mo -Maximum).Maximum
        Empreinte   = ($g.Group | Where-Object { $_.Empreinte } | Select-Object -First 1).Empreinte
    })
}
$summary | Sort-Object Projet, Threads, Logiciel | Export-Csv -Path (Join-Path $Out "resume.csv") -NoTypeInformation -Delimiter ";" -Encoding UTF8

Write-Host ""
Write-Host "================ Resume (mediane) ================"
$summary | Sort-Object Projet, Threads, Logiciel | Format-Table Projet, Logiciel, Threads, Mediane_s, Threads_moy, RAM_max_Mo, Empreinte -AutoSize | Out-String | Write-Host

$compare = @()
foreach ($pg in ($summary | Group-Object Projet, Threads)) {
    $o = $pg.Group | Where-Object { $_.Logiciel -eq "Orca" } | Select-Object -First 1
    $z = $pg.Group | Where-Object { $_.Logiciel -eq "ZEBU" } | Select-Object -First 1
    if ($o -and $z -and $z.Mediane_s -gt 0) {
        $same = "DIFFERENT"
        if ($o.Empreinte -and $o.Empreinte -eq $z.Empreinte) { $same = "identique" }
        $compare += ("{0} ({1} threads) : Orca {2:N1} s, ZEBU {3:N1} s, ZEBU x{4:N2} ; G-code {5}" -f `
            $o.Projet, $o.Threads, $o.Mediane_s, $z.Mediane_s, ($o.Mediane_s / $z.Mediane_s), $same)
    }
}
if ($compare.Count -gt 0) {
    Write-Host "================ ZEBU / Orca ================"
    $compare | ForEach-Object { Write-Host $_ }
    $compare | Set-Content -Path (Join-Path $Out "comparaison.txt") -Encoding UTF8
}
Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ""
Write-Host "Resultats : $Out  (envoie machine.txt, resume.csv et comparaison.txt)"
