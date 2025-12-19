plugins {
    id("com.android.application")
}

android {
    namespace = "com.msplat.gaussiansplatting"
    compileSdk = 34

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
tasks.register<Copy>("copyModels") {
    from("../../assets/splats/flowers_1")
    into("src/main/assets/models")
    include("*.ply")
}

// Make asset copy run before asset merging
tasks.whenTaskAdded {
    if (name.contains("merge") && name.contains("Assets")) {
        dependsOn("copyShaders")
        dependsOn("copyModels")
    }
}
