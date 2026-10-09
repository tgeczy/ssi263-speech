// The app's import as the app runs it -- FirmwareInstaller.inspect and Job.commit over the real native side
// (SSI263Core's macos-arm64 slice) -- on the repository's own firmware, in a folder of its own in place of the App
// Group's: each unit's files end up in the unit folder, labelled, and SsiShared sees each voice as installed.
// Skips a unit whose firmware is not in firmware/.   sh src/platforms/apple/test/test_installer.sh
import Foundation

let repo = URL(fileURLWithPath: CommandLine.arguments[1])
let root = FileManager.default.temporaryDirectory.appendingPathComponent("ssi263-installer-\(getpid())")
try? FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
SsiShared.containerOverride = root
var failures = 0

func run(_ label: String, _ files: [String], expect: [String]) {
    let urls = files.map { repo.appendingPathComponent("firmware/" + $0) }
    guard urls.allSatisfy({ FileManager.default.fileExists(atPath: $0.path) }) else {
        print("skip  \(label): its firmware is not in firmware/")
        return
    }
    do {
        let plan = try FirmwareInstaller.inspect(urls)
        if let r = plan.refusal { throw FirmwareInstaller.Failure(message: "refused: " + r) }
        let job = FirmwareInstaller.Job(plan: plan)
        let labels = try job.commit()
        let installed = SsiShared.voices.filter(SsiShared.installed).map { $0.key }
        let ok = expect.allSatisfy(installed.contains)
        print("\(ok ? "ok   " : "FAIL ") \(label): \(labels.joined(separator: "; ")); installed: \(installed)")
        if !ok { failures += 1 }
    } catch {
        print("FAIL  \(label): \(error.localizedDescription)")
        failures += 1
    }
}

run("the Speak-Out", ["gw-micro-speakout/SPEAKOUT.HEX"], expect: ["speakout"])
run("the Accents", ["aicom-accent-sa/u2.BIN", "aicom-accent-sa/u3.BIN", "aicom-accent-sa/u4.BIN",
                    "aicom-accent-mini/SPKEMS.DVC"], expect: ["accentsa", "accentmini"])
run("the English Braille Lite", ["blazie/BL2ENG.BNS"], expect: ["braillelite"])
try? FileManager.default.removeItem(at: root)
print(failures == 0 ? "the import installs every unit" : "FAILED: \(failures)")
exit(failures == 0 ? 0 : 1)
