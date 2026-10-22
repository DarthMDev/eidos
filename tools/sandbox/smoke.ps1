# Runs inside Windows Sandbox (tools/sandbox/eidos-smoke.wsb).  Unzips the
# release package, starts eidos headless, and writes the result to the
# mapped out folder.
$ErrorActionPreference = 'Stop'
$in  = 'C:\Users\WDAGUtilityAccount\Desktop\in'
$out = 'C:\Users\WDAGUtilityAccount\Desktop\out'
$dst = 'C:\Users\WDAGUtilityAccount\Desktop\eidos'
$log = Join-Path $out 'smoke.log'
$res = Join-Path $out 'smoke-result.txt'
Remove-Item $log, $res -ErrorAction SilentlyContinue

try {
    $zip = Get-ChildItem $in -Filter 'eidos-*-windows.zip' | Select-Object -First 1
    if (-not $zip) { throw "no eidos-*-windows.zip in $in" }
    "package: $($zip.Name)" | Tee-Object -FilePath $log -Append
    Expand-Archive -Path $zip.FullName -DestinationPath $dst -Force
    Get-ChildItem $dst | Format-Table Name, Length | Out-String | Tee-Object -FilePath $log -Append

    $env:EIDOS_WINDOW_HIDDEN = '1'
    $env:BW_SAVE_DIR = Join-Path $dst 'smoke-saves'
    New-Item -ItemType Directory -Force $env:BW_SAVE_DIR | Out-Null
    $data = Join-Path $dst 'no-data'
    New-Item -ItemType Directory -Force $data | Out-Null

    $p = Start-Process -FilePath (Join-Path $dst 'eidos.exe') `
        -ArgumentList @('--data-root', $data, '--frames', '300') `
        -WorkingDirectory $dst -PassThru -Wait `
        -RedirectStandardOutput (Join-Path $out 'stdout.txt') `
        -RedirectStandardError  (Join-Path $out 'stderr.txt')
    "exit code: $($p.ExitCode)" | Tee-Object -FilePath $log -Append
    # 0: ran to the end.  Anything else is reported, with the output files.
    $verdict = if ($p.ExitCode -eq 0) { 'PASS' } else { 'FAIL' }
}
catch {
    $_ | Out-String | Tee-Object -FilePath $log -Append
    $verdict = 'FAIL'
}
$verdict | Set-Content $res
"result: $verdict" | Tee-Object -FilePath $log -Append
