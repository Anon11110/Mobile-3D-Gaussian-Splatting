plugins {
    id("com.android.application")
}

// Find the newest NDK >= r27 (version 27.x.x)
fun findNewestNdk(minMajorVersion: Int = 27): String? {
    val sdkDir = System.getenv("ANDROID_HOME")
        ?: System.getenv("ANDROID_SDK_ROOT")
        ?: return null
    val ndkDir = File(sdkDir, "ndk")
    if (!ndkDir.exists()) return null

    return ndkDir.listFiles()
        ?.filter { it.isDirectory }
        ?.mapNotNull { dir ->
            val parts = dir.name.split(".")
            val major = parts.getOrNull(0)?.toIntOrNull()
            if (major != null && major >= minMajorVersion) {
                major to dir.name
            } else null
        }
        ?.maxByOrNull { it.first }
        ?.second
}

android {
    namespace = "com.msplat.gaussiansplatting"
    compileSdk = 34
    ndkVersion = findNewestNdk(27) ?: "27.0.0"

    defaultConfig {
        applicationId = "com.msplat.gaussiansplatting"
        minSdk = 24  // Vulkan 1.0 support requires API 24+
        targetSdk = 34
        versionCode = 1
        versionName = "1.0.0"

        ndk {
            // Build for 64-bit ARM (most modern devices)
            // Add "armeabi-v7a" for 32-bit ARM support if needed
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_PLATFORM=android-24",
                    "-DCMAKE_BUILD_TYPE=Release"
                )
                cppFlags += listOf("-std=c++20", "-frtti", "-fexceptions")
            }
        }
    }

    buildTypes {
        debug {
            isDebuggable = true
            isJniDebuggable = true
            externalNativeBuild {
                cmake {
                    arguments += "-DCMAKE_BUILD_TYPE=Debug"
                }
            }
        }
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("debug")  // Use debug signing for testing
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            externalNativeBuild {
                cmake {
                    arguments += "-DCMAKE_BUILD_TYPE=Release"
                }
            }
        }
    }

    // Disable lint for this native app (avoids task dependency issues with asset copying)
    lint {
        checkReleaseBuilds = false
        abortOnError = false
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    // Copy compiled shaders to assets
    sourceSets {
        getByName("main") {
            assets.srcDirs("src/main/assets")
        }
    }
}

// Task to copy shaders to assets directory
tasks.register<Copy>("copyShaders") {
    // Copy shared shaders from main shaders directory
    from("../../shaders/compiled")
    into("src/main/assets/shaders/compiled")
    include("*.spv")
}

// Task to copy PLY model files to assets directory
tasks.register("copyModels") {
    val modelsDir = file("src/main/assets/models")

    doLast {
        modelsDir.mkdirs()

        // Copy flowers_1.ply
        copy {
            from("../../assets/splats/flowers_1/flowers_1.ply")
            into(modelsDir)
        }

        // Copy train_30000.ply (rename from point_cloud.ply)
        copy {
            from("../../assets/splats/train/point_cloud/iteration_30000/point_cloud.ply")
            into(modelsDir)
            rename { "train_30000.ply" }
        }
    }
}

// Make asset copy run before asset merging
tasks.whenTaskAdded {
    if (name.contains("merge") && name.contains("Assets")) {
        dependsOn("copyShaders")
        dependsOn("copyModels")
    }
}
