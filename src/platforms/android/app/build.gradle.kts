import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Everything the APK carries besides code is staged by build_android.sh at the repository root into
// build/android/assets: the Accent SA's ROMs (Aicom's, the built-in voice), the Mockingboards' files when they were
// there to stage (Sweet Micro's, assets/sweet-micro), and the licences (MIT, MAME's BSD notices, Sweet Micro's notice).
// No Braille Lite or Speak-Out firmware: the app's users import their own (FirmwareImport.kt); only a
// developer build asked for with SSI263_ANDROID_BUNDLE_FIRMWARE=1 carries it.  The native library, from the same
// sources as the Linux and Windows builds, is dropped under jniLibs.  Gradle only checks both are there.
val repoRoot = rootProject.file("../../..")
val stagedAssets = File(repoRoot, "build/android/assets")
val abis = listOf("arm64-v8a", "armeabi-v7a", "x86_64")
val verifyNativeBuild = tasks.register("verifyNativeBuild") {
    doLast {
        for (abi in abis) {
            val library = file("src/main/jniLibs/$abi/libssi263speech.so")
            check(library.isFile) { "Build the $abi library with `sh build_android.sh` first" }
        }
        for (name in listOf("licenses/DISTRIBUTION.txt", "licenses/ssi263-speech-MIT.txt",
                            "licenses/Aicom-Accent-SA-notice.txt", "licenses/MAME-Z180-core-BSD-3-Clause.txt",
                            "licenses/MAME-8085-core-BSD-3-Clause.txt", "licenses/MAME-NEC-V40-core-BSD-3-Clause.txt",
                            "licenses/Casso-MIT.txt",
                            "aicom/u2.BIN", "aicom/u3.BIN", "aicom/u4.BIN") +
                            // the Accent-mini, built in once its voice's sources are in the tree (build_android.sh)
                            (if (File(repoRoot, "src/csrc/accentmini/am_voice.c").isFile)
                                listOf("aicom/SPKEMS.DVC", "licenses/MAME-8086-core-BSD-3-Clause.txt") else emptyList()) +
                            // the Mockingboard's 6502 credits, once its voice's sources are in the tree (its
                            // firmware is the user's to import)
                            (if (File(repoRoot, "src/csrc/mockingboard/mb_voice.c").isFile)
                                listOf("licenses/Fake6502-6502-core.txt", "licenses/EchoTalk-BSD-3-Clause.txt")
                            else emptyList())) {
            check(File(stagedAssets, name).isFile) { "build/android/assets/$name is missing: run `sh build_android.sh`" }
        }
        // All-MAME 0.7: no GPL code, so nothing of 0.6's GPL staging may ride along from an older stage
        for (name in listOf("licenses/z180emu-GPL-2.0.txt", "source/ssi263-speech-source.tgz")) {
            check(!File(stagedAssets, name).exists()) { "build/android/assets/$name is stale: run `sh build_android.sh`" }
        }
        // A developer build's staged firmware must not ride into an APK unasked: Gradle packs whatever is staged, so
        // a leftover of SSI263_ANDROID_BUNDLE_FIRMWARE=1 went into a plain assembleDebug (0.7).  Ask with
        // -Pssi263BundleFirmware=1, or re-stage with `sh build_android.sh`.
        val staged = File(stagedAssets, "firmware").listFiles()?.filter { it.isFile }.orEmpty()
        check(staged.isEmpty() || project.hasProperty("ssi263BundleFirmware")) {
            "build/android/assets/firmware holds ${staged.map { it.name }}: a developer build's firmware. " +
                "Re-stage with `sh build_android.sh`, or bundle it on purpose with -Pssi263BundleFirmware=1 " +
                "(never a release)"
        }
        // The Mockingboards' files (Sweet Micro's, carried by the GitHub APK: Tomi, 2026-10-10) only with their notice
        val sweet = File(stagedAssets, "sweet-micro").listFiles()?.filter { it.isFile }.orEmpty()
        check(sweet.isEmpty() || File(stagedAssets, "licenses/Sweet-Micro-Mockingboard-notice.txt").isFile) {
            "build/android/assets/sweet-micro holds ${sweet.map { it.name }} without Sweet Micro's notice: " +
                "run `sh build_android.sh`"
        }
    }
}
tasks.matching { it.name == "preBuild" }.configureEach { dependsOn(verifyNativeBuild) }

android {
    namespace = "com.ssi263speech.tts"
    compileSdk = 35

    defaultConfig {
        // Permanent once shipped: Android treats a different id as a different app.
        applicationId = "com.ssi263speech.tts"
        minSdk = 26
        targetSdk = 35
        versionCode = 5
        versionName = "0.8.0"

        ndk {
            abiFilters += abis
        }
    }

    // Kotlin sources live under src/main/kotlin; the JVM tests (the import's logic, no phone) under src/test/kotlin.
    sourceSets["main"].java.srcDirs("src/main/kotlin")
    sourceSets["test"].java.srcDirs("src/test/kotlin")
    sourceSets["main"].assets.srcDir(stagedAssets)

    // The firmware, states and ROMs are read as they are: no compression, so they copy out of the APK quickly.
    androidResources {
        noCompress += listOf("BNS", "state", "tgz", "BIN", "bin")
    }

    // Release signing, as outspoken's and TGSpeechBox's builds: a `signing.properties` beside settings.gradle.kts,
    // ignored by Git, naming the key -- STORE_FILE, STORE_PASSWORD, KEY_ALIAS, KEY_PASSWORD.  Without it the release
    // build still succeeds and stays unsigned.  The same key must sign every future release.
    val signingProperties = rootProject.file("signing.properties")
    if (signingProperties.isFile) {
        val keys = Properties().apply { signingProperties.inputStream().use { load(it) } }
        signingConfigs {
            create("release") {
                storeFile = file(keys.getProperty("STORE_FILE"))
                storePassword = keys.getProperty("STORE_PASSWORD")
                keyAlias = keys.getProperty("KEY_ALIAS")
                keyPassword = keys.getProperty("KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            if (signingProperties.isFile) signingConfig = signingConfigs.getByName("release")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
}

// The JVM tests' must-fail controls: -Pssi263ImportBreak=1 switches FirmwareImport's layout rules off,
// -Pssi263ImportBreak=state its knowing a state file.
tasks.withType<Test>().configureEach {
    if (project.hasProperty("ssi263ImportBreak"))
        systemProperty("ssi263.import.break", project.property("ssi263ImportBreak").toString())
}
