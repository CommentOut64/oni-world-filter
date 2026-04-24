param(
    [Parameter(Mandatory = $true)]
    [string]$SidecarPath,
    [int]$WorldType = 13,
    [int]$Mixing = 625,
    [int]$SeedStart = 100000,
    [int]$SeedEnd = 300000,
    [int]$Threads = 1,
    [int]$CancelDelayMs = 100,
    [int]$GraceTimeoutMs = 75,
    [switch]$WaitForFirstMatchBeforeCancel,
    [int]$FirstMatchTimeoutMs = 15000,
    [int]$PostFirstMatchDelayMs = 0,
    [int]$DrainTimeoutMs = 5000,
    [int]$MaxTerminalLatencyMs = 1000
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not ("SidecarEventCapture" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text.RegularExpressions;
using System.Threading;

public sealed class SidecarEventCapture
{
    private static readonly Regex EventNamePattern = new Regex("\"event\"\\s*:\\s*\"([^\"]+)\"", RegexOptions.Compiled);

    public readonly ConcurrentQueue<string> StdoutLines = new ConcurrentQueue<string>();
    public readonly ConcurrentQueue<string> StderrLines = new ConcurrentQueue<string>();
    public readonly ConcurrentQueue<string> ParseErrors = new ConcurrentQueue<string>();
    public readonly ConcurrentQueue<DateTime> MatchReceivedAtUtc = new ConcurrentQueue<DateTime>();
    public readonly ManualResetEventSlim StdoutClosed = new ManualResetEventSlim(false);
    public readonly ManualResetEventSlim StderrClosed = new ManualResetEventSlim(false);

    private int _matchCount;
    private string _terminalEventName;
    private DateTime? _firstMatchAtUtc;
    private DateTime? _terminalAtUtc;
    private DateTime? _stdoutClosedAtUtc;

    public int MatchCount
    {
        get { return _matchCount; }
    }

    public string TerminalEventName
    {
        get { return Volatile.Read(ref _terminalEventName); }
    }

    public DateTime? TerminalAtUtc
    {
        get { return _terminalAtUtc; }
    }

    public DateTime? FirstMatchAtUtc
    {
        get { return _firstMatchAtUtc; }
    }

    public DateTime? StdoutClosedAtUtc
    {
        get { return _stdoutClosedAtUtc; }
    }

    public int ParseErrorCount
    {
        get { return ParseErrors.Count; }
    }

    public void Attach(Process process)
    {
        process.OutputDataReceived += HandleOutputData;
        process.ErrorDataReceived += HandleErrorData;
    }

    private void HandleOutputData(object sender, DataReceivedEventArgs args)
    {
        if (args.Data == null)
        {
            _stdoutClosedAtUtc = DateTime.UtcNow;
            StdoutClosed.Set();
            return;
        }

        var line = args.Data.Trim();
        if (line.Length == 0)
        {
            return;
        }

        var receivedAtUtc = DateTime.UtcNow;
        StdoutLines.Enqueue(line);
        var match = EventNamePattern.Match(line);
        if (!match.Success)
        {
            ParseErrors.Enqueue(line);
            return;
        }

        var eventName = match.Groups[1].Value;
        if (string.Equals(eventName, "match", StringComparison.Ordinal))
        {
            MatchReceivedAtUtc.Enqueue(receivedAtUtc);
            if (!_firstMatchAtUtc.HasValue)
            {
                _firstMatchAtUtc = receivedAtUtc;
            }
            Interlocked.Increment(ref _matchCount);
            return;
        }

        if (string.Equals(eventName, "completed", StringComparison.Ordinal) ||
            string.Equals(eventName, "failed", StringComparison.Ordinal) ||
            string.Equals(eventName, "cancelled", StringComparison.Ordinal))
        {
            if (Interlocked.CompareExchange(ref _terminalEventName, eventName, null) == null)
            {
                _terminalAtUtc = receivedAtUtc;
            }
        }
    }

    private void HandleErrorData(object sender, DataReceivedEventArgs args)
    {
        if (args.Data == null)
        {
            StderrClosed.Set();
            return;
        }

        var line = args.Data.Trim();
        if (line.Length == 0)
        {
            return;
        }

        StderrLines.Enqueue(line);
    }

    public int CountMatchesOnOrAfter(DateTime thresholdUtc)
    {
        var count = 0;
        foreach (var receivedAtUtc in MatchReceivedAtUtc)
        {
            if (receivedAtUtc >= thresholdUtc)
            {
                count++;
            }
        }
        return count;
    }
}
"@
}

function Get-SearchRequestJson {
    param(
        [Parameter(Mandatory = $true)][string]$JobId
    )

    return (@{
        command = "search"
        jobId = $JobId
        worldType = $WorldType
        seedStart = $SeedStart
        seedEnd = $SeedEnd
        mixing = $Mixing
        threads = $Threads
        constraints = @{
            required = @()
            forbidden = @()
            distance = @()
            count = @()
        }
    } | ConvertTo-Json -Compress -Depth 20)
}

function Get-CancelRequestJson {
    param(
        [Parameter(Mandatory = $true)][string]$JobId
    )

    return (@{
        command = "cancel"
        jobId = $JobId
    } | ConvertTo-Json -Compress -Depth 10)
}

function Close-ProcessInput {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process
    )

    try {
        if ($null -ne $Process.StandardInput) {
            $Process.StandardInput.Close()
        }
    }
    catch {
    }
}

function Wait-TerminalEvent {
    param(
        [Parameter(Mandatory = $true)]
        [SidecarEventCapture]$Capture,
        [Parameter(Mandatory = $true)]
        [int]$TimeoutMs
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($null -ne $Capture.TerminalEventName) {
            return $true
        }
        Start-Sleep -Milliseconds 10
    }
    return ($null -ne $Capture.TerminalEventName)
}

function Wait-FirstMatch {
    param(
        [Parameter(Mandatory = $true)]
        [SidecarEventCapture]$Capture,
        [Parameter(Mandatory = $true)]
        [int]$TimeoutMs
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($null -ne $Capture.FirstMatchAtUtc) {
            return $true
        }
        Start-Sleep -Milliseconds 10
    }
    return ($null -ne $Capture.FirstMatchAtUtc)
}

function Convert-QueueToArray {
    param(
        [Parameter(Mandatory = $true)]
        [System.Collections.Concurrent.ConcurrentQueue[string]]$Queue
    )

    return @($Queue.ToArray())
}

$resolvedSidecarPath = (Resolve-Path -LiteralPath $SidecarPath).Path
if (-not (Test-Path -LiteralPath $resolvedSidecarPath)) {
    throw "sidecar binary not found: $SidecarPath"
}

$jobId = "cancel-latency-{0}" -f ([guid]::NewGuid().ToString("N"))
$capture = [SidecarEventCapture]::new()

$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $resolvedSidecarPath
$startInfo.WorkingDirectory = Split-Path -Path $resolvedSidecarPath -Parent
$startInfo.UseShellExecute = $false
$startInfo.RedirectStandardInput = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$startInfo.CreateNoWindow = $true

$process = [System.Diagnostics.Process]::new()
$process.StartInfo = $startInfo
$capture.Attach($process)
$process.Start() | Out-Null
$process.BeginOutputReadLine()
$process.BeginErrorReadLine()

$searchSentAtUtc = $null
$cancelSentAtUtc = $null
$killSentAtUtc = $null
$usedKill = $false
$synthesizedTerminal = $false

try {
    $searchSentAtUtc = [DateTime]::UtcNow
    $process.StandardInput.WriteLine((Get-SearchRequestJson -JobId $jobId))
    $process.StandardInput.Flush()

    if ($WaitForFirstMatchBeforeCancel) {
        if (-not (Wait-FirstMatch -Capture $capture -TimeoutMs $FirstMatchTimeoutMs)) {
            throw "first match was not observed within $FirstMatchTimeoutMs ms"
        }
        if ($PostFirstMatchDelayMs -gt 0) {
            Start-Sleep -Milliseconds $PostFirstMatchDelayMs
        }
    } else {
        Start-Sleep -Milliseconds $CancelDelayMs
    }

    $cancelSentAtUtc = [DateTime]::UtcNow
    $process.StandardInput.WriteLine((Get-CancelRequestJson -JobId $jobId))
    $process.StandardInput.Flush()

    if (-not (Wait-TerminalEvent -Capture $capture -TimeoutMs $GraceTimeoutMs)) {
        Close-ProcessInput -Process $process
        if (-not $process.HasExited) {
            $killSentAtUtc = [DateTime]::UtcNow
            $process.Kill()
            $usedKill = $true
        }
    }

    if (-not $capture.StdoutClosed.Wait($DrainTimeoutMs)) {
        throw "stdout drain timed out after $DrainTimeoutMs ms"
    }
    if (-not $capture.StderrClosed.Wait($DrainTimeoutMs)) {
        throw "stderr drain timed out after $DrainTimeoutMs ms"
    }

    if (-not $process.WaitForExit($DrainTimeoutMs)) {
        throw "sidecar process did not exit within $DrainTimeoutMs ms"
    }

    $terminalEventName = $capture.TerminalEventName
    $terminalAtUtc = $capture.TerminalAtUtc
    if ([string]::IsNullOrWhiteSpace($terminalEventName)) {
        $terminalEventName = "cancelled"
        $terminalAtUtc = if ($null -ne $capture.StdoutClosedAtUtc) {
            $capture.StdoutClosedAtUtc
        } else {
            [DateTime]::UtcNow
        }
        $synthesizedTerminal = $true
    }

    if ($null -eq $cancelSentAtUtc) {
        throw "cancel timestamp was not recorded"
    }

    $matchesAfterCancel = $capture.CountMatchesOnOrAfter($cancelSentAtUtc)
    $matchesAfterKill = if ($null -ne $killSentAtUtc) {
        $capture.CountMatchesOnOrAfter($killSentAtUtc)
    } else {
        0
    }
    $matchesBeforeCancel = $capture.MatchCount - $matchesAfterCancel
    $firstMatchLatencyMs = if ($null -ne $capture.FirstMatchAtUtc -and $null -ne $searchSentAtUtc) {
        [math]::Round((($capture.FirstMatchAtUtc - $searchSentAtUtc).TotalMilliseconds), 1)
    } else {
        $null
    }
    $terminalLatencyMs = [math]::Round((($terminalAtUtc - $cancelSentAtUtc).TotalMilliseconds), 1)
    $stderrTail = Convert-QueueToArray -Queue $capture.StderrLines | Select-Object -Last 10
    $result = [pscustomobject][ordered]@{
        sidecarPath = $resolvedSidecarPath
        jobId = $jobId
        waitedForFirstMatch = [bool]$WaitForFirstMatchBeforeCancel
        firstMatchTimeoutMs = $FirstMatchTimeoutMs
        postFirstMatchDelayMs = $PostFirstMatchDelayMs
        firstMatchAtMsFromSearch = $firstMatchLatencyMs
        cancelDelayMs = $CancelDelayMs
        graceTimeoutMs = $GraceTimeoutMs
        cancelToTerminalMs = $terminalLatencyMs
        usedKill = $usedKill
        synthesizedTerminal = $synthesizedTerminal
        matchCount = $capture.MatchCount
        matchesBeforeCancel = $matchesBeforeCancel
        matchesAfterCancel = $matchesAfterCancel
        matchesAfterKill = $matchesAfterKill
        terminalEvent = $terminalEventName
        processExitCode = $process.ExitCode
        stdoutLineCount = $capture.StdoutLines.Count
        stderrLineCount = $capture.StderrLines.Count
        parseErrorCount = $capture.ParseErrorCount
        stderrTail = @($stderrTail)
    }

    Write-Host ("[smoke] sidecar={0}" -f $resolvedSidecarPath)
    Write-Host ("[smoke] cancel -> terminal = {0} ms | kill={1} | matches={2} | lateAfterCancel={3} | lateAfterKill={4} | terminal={5}" -f $terminalLatencyMs, $usedKill, $capture.MatchCount, $matchesAfterCancel, $matchesAfterKill, $terminalEventName)
    $result | ConvertTo-Json -Depth 10

    if ($terminalEventName -ne "cancelled") {
        throw "expected terminal event cancelled, got $terminalEventName"
    }
    if ($terminalLatencyMs -gt $MaxTerminalLatencyMs) {
        throw "cancel latency $terminalLatencyMs ms exceeded hard limit $MaxTerminalLatencyMs ms"
    }
}
finally {
    Close-ProcessInput -Process $process
    if (-not $process.HasExited) {
        try {
            $process.Kill()
        }
        catch {
        }
    }
    $capture.StdoutClosed.Dispose()
    $capture.StderrClosed.Dispose()
    $process.Dispose()
}
