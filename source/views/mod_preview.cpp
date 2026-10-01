#include "views/mod_preview.hpp"
#include "views/download_view.hpp"
#include "views/spinner_image_view.hpp"
#include "utils/utils.hpp"
#include "utils/progress_event.hpp"
#include "utils/config.hpp"
#include "utils/version_check.hpp"
#include "api/net.hpp"
#include "api/mod.hpp"
#include <fmt/format.h>
#include <curl/curl.h>

#include <future>
#include <set>

using namespace brls::literals;

// ─────────────────────────────────────────────
// FileBox
// ─────────────────────────────────────────────

FileBox::FileBox(File& file) {
    this->inflateFromXMLRes("xml/cells/file_cell.xml");
    this->setFocusable(false);

    #ifndef NDEBUG
    cfg::Config config;
    if (config.getWireframe()) {
        this->setWireframeEnabled(true);
        for(auto& view : this->getChildren()) {
            view->setWireframeEnabled(true);
        }
    }
    #endif

    auto* titleLabel = dynamic_cast<brls::Label*>(this->getView("title"));
    auto* gameVersionLabel = dynamic_cast<brls::Label*>(this->getView("game_version"));
    auto* translationVersionLabel = dynamic_cast<brls::Label*>(this->getView("translation_version"));
    auto* downloadButton = dynamic_cast<brls::Button*>(this->getView("download"));

    std::string gameVer = file.getGameVersion();
    std::string transVer = file.getTranslationVersion();

    std::string versionInfo;
    if (!gameVer.empty()) versionInfo += fmt::format("Версія гри: {}", gameVer);
    if (!transVer.empty()) {
        if (!versionInfo.empty()) versionInfo += "\n";
        versionInfo += fmt::format("Версія перекладу: {}", transVer);
    }

    if (titleLabel) titleLabel->setText(versionInfo.empty() ? file.getName() : versionInfo);
    if (gameVersionLabel) gameVersionLabel->setText(gameVer.empty() ? "" : fmt::format("Гра: v{}", gameVer));
    if (translationVersionLabel) translationVersionLabel->setText(transVer.empty() ? "" : fmt::format("Переклад: v{}", transVer));
    if (downloadButton) {
        downloadButton->setText("menu/mods/download"_i18n);
        downloadButton->setFocusable(true);
    }
}

// ─────────────────────────────────────────────
// ModPreview
// ─────────────────────────────────────────────

ModPreview::ModPreview(Mod& mod, std::vector<unsigned char>& bannerBuffer, bool readOnly): mod(mod), readOnly(readOnly) {

#ifdef BOTW_BUILD
    // Files already set in mod object from mod.cpp — skip API call
#else
    // Load custom Ukrainian translation details from API
    if (this->mod.getID() == -1) {
        std::string detailUrl = fmt::format(
            "https://swuk.com.ua/wp-json/custom-api/v1/title-translations/author?titleId={}&authorId={}",
            this->mod.getGame().getTid(),
            this->mod.author_id);

        brls::Logger::info("Fetching custom mod details from: {}", detailUrl);

        try {
            auto response = net::downloadRequest(detailUrl);
            brls::Logger::debug("API response received. Is array: {}, Empty: {}", response.is_array(), response.empty());

            if (response.is_array() && !response.empty()) {
                this->mod.files.clear();
                this->mod.imageUrls.clear();

                std::set<std::string> uniqueScreenshots;
                for (size_t idx = 0; idx < response.size(); idx++) {
                    auto detail = response[idx];
                    if (detail.contains("screenshot_urls")) {
                        try {
                            auto screenshotUrls = detail["screenshot_urls"];
                            if (screenshotUrls.is_array()) {
                                for (auto& url : screenshotUrls)
                                    if (url.is_string()) uniqueScreenshots.insert(url.template get<std::string>());
                            } else if (screenshotUrls.is_string()) {
                                auto screenArray = nlohmann::json::parse(screenshotUrls.template get<std::string>());
                                if (screenArray.is_array())
                                    for (auto& url : screenArray)
                                        if (url.is_string()) uniqueScreenshots.insert(url.template get<std::string>());
                            }
                        } catch (const std::exception& e) {
                            brls::Logger::error("Failed to parse screenshots for entry #{}: {}", idx, e.what());
                        }
                    }
                }

                for (const auto& url : uniqueScreenshots)
                    this->mod.imageUrls.push_back(url);
                brls::Logger::debug("Successfully collected {} unique screenshot URLs", this->mod.imageUrls.size());

                for (size_t idx = 0; idx < response.size(); idx++) {
                    auto detail = response[idx];
                    brls::Logger::debug("Processing detail object #{}", idx);

                    std::string gameVer = detail.value("game_version", "1.0.0");
                    std::string transVer = detail.value("translation_version", "1.0.0");
                    brls::Logger::info("Entry #{}: gameVer={}, transVer={}", idx, gameVer, transVer);

                    if (idx == 0) this->mod.game_version = gameVer;

                    if (detail.contains("download_url") && detail["download_url"].is_string()) {
                        std::string dUrl = detail["download_url"].template get<std::string>();
                        if (!dUrl.empty()) {
                            std::string fileName = fmt::format("translation_{}_v{}.zip",
                                this->mod.author_id.substr(0, 8), transVer);
                            std::string fileDesc = fmt::format("{}\n\nГра: {}\nПереклад: {}",
                                this->mod.getName(), gameVer, transVer);

                            File modFile(fileName, 0, dUrl, "", fileDesc, 0, "-1", this->mod.getGame());
                            modFile.romfs = true;
                            modFile.setGameVersion(gameVer);
                            modFile.setTranslationVersion(transVer);
                            this->mod.files.push_back(modFile);
                            brls::Logger::debug("Added download file #{}: {} from URL: {}", idx, fileName, dUrl);
                        }
                    }
                }

                brls::Logger::info("Mod details loaded - Total entries: {}, Files: {}, Unique screenshots: {}",
                    response.size(), this->mod.files.size(), this->mod.imageUrls.size());
            }
        } catch (const std::exception& e) {
            brls::Logger::error("Exception in ModPreview constructor: {}", e.what());
        }
    } else {
        this->mod.loadMod();
    }
#endif

    // ── UI setup ──────────────────────────────────────────────────────────────
    this->inflateFromXMLRes("xml/tabs/mod_preview.xml");

    this->setFocusable(false);
    scrolling->setFocusable(true);

    banner->setImageFromMem(bannerBuffer.data(), bannerBuffer.size());
    banner->setHeight(200);

    image_overlay->setColor(nvgRGBA(100, 100, 100, 0.8*255));
    title->setText(this->mod.getName());
    description->setText(this->mod.getDescription());

    if (readOnly) {
        auto* filesHeader = this->getView("files_header");
        if (filesHeader) filesHeader->setVisibility(brls::Visibility::GONE);
        files_box->setVisibility(brls::Visibility::GONE);
    }

    secondThread = std::thread(&ModPreview::loadImages, this);

    // Hide header — identical to original pattern
    brls::sync([this] {
        getAppletFrame()->setHeaderVisibility(brls::Visibility::GONE);
    });

    // Back action — identical to original pattern
    this->registerAction("back", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
        brls::sync([this]{ getAppletFrame()->setHeaderVisibility(brls::Visibility::VISIBLE); });
        this->dismiss();
        return true;
    }, true);
}

// ─────────────────────────────────────────────
// loadButtons
// ─────────────────────────────────────────────

void ModPreview::loadButtons() {
    for (auto file : this->mod.getFiles()) {
        auto fileBox = new FileBox(file);
        fileBox->getDownloadButton()->registerClickAction(brls::ActionListener([file = std::move(file), this](brls::View* view) mutable {
            brls::Logger::debug("File clicked : {}", file.getName());

#ifdef BOTW_BUILD
            getAppletFrame()->setHeaderVisibility(brls::Visibility::VISIBLE);
            ProgressEvent::instance().setInterupt(true);
            this->present(new DownloadView(file));
            this->stopThreadFlag = true;
            return true;
#endif

            std::string gameTid = file.getGame().getTid();

            if (this->mod.getID() == -1 || file.getGame().getGamebananaID() == 1) {

                if (version::isGameInstalled(gameTid)) {
                    std::string installedVer = version::getInstalledGameVersion(gameTid);
                    std::string requiredVer = file.getGameVersion();

                    brls::Logger::debug("Version check - Installed: '{}', Required: '{}'", installedVer, requiredVer);

                    if (!installedVer.empty() && installedVer != "Unknown" && !requiredVer.empty()) {
                        int versionMatch = version::compareVersions(installedVer, requiredVer);
                        brls::Logger::debug("Version comparison result: {}", versionMatch);

                        if (versionMatch != 0) {
                            brls::sync([installedVer, requiredVer, file, this]() mutable {
                                std::string warningMsg = fmt::format(
                                    "Встановлена версія гри: {}\n"
                                    "Необхідна версія: {}\n\n"
                                    "Версії не співпадають. Мод може працювати неправильно.\n\n"
                                    "Продовжити встановлення?",
                                    installedVer, requiredVer);

                                auto dialog = new brls::Dialog(warningMsg);
                                dialog->addButton("Скасувати", []() {});
                                dialog->addButton("Так", [file = std::move(file), this]() mutable {
                                    file.loadFile();
                                    if (file.getRomfs() || file.getFileID() == "-1") {
                                        this->validateAndDownload(file);
                                    } else {
                                        auto d = new brls::Dialog("Мод не підтримується");
                                        d->addButton("OK", []() {});
                                        d->open();
                                    }
                                });
                                dialog->open();
                            });
                            return true;
                        }
                    }

                    file.loadFile();
                    if (file.getRomfs() || file.getFileID() == "-1") {
                        this->validateAndDownload(file);
                    } else {
                        auto d = new brls::Dialog("Мод не підтримується");
                        d->addButton("OK", []() {});
                        d->open();
                    }

                } else {
                    brls::sync([gameTid]() {
                        auto dialog = new brls::Dialog(fmt::format(
                            "Гра не встановлена\n\nTitle ID: {}\n\n"
                            "Будь ласка, встановіть гру перед встановленням моду.", gameTid));
                        dialog->addButton("OK", []() {});
                        dialog->open();
                    });
                }

            } else {
                // Original GameBanana logic
                file.loadFile();
                if (file.getRomfs() || file.getGame().getTid() == "01006A800016E000" || file.getFileID() == "-1") {
                    brls::sync([this] {
                        getAppletFrame()->setHeaderVisibility(brls::Visibility::VISIBLE);
                    });
                    ProgressEvent::instance().setInterupt(true);
                    this->present(new DownloadView(file));
                    this->stopThreadFlag = true;
                } else {
                    auto dialog = new brls::Dialog("Mod не підтримується");
                    dialog->addButton("OK", []() {});
                    dialog->open();
                }
            }

            return true;
        }));
        files_box->addView(fileBox);
    }
}

// ─────────────────────────────────────────────
// validateAndDownload  (extracted helper)
// ─────────────────────────────────────────────

void ModPreview::validateAndDownload(File& file) {
    std::string downloadUrl = file.getUrl();

    CURL* curl = curl_easy_init();
    if (!curl) {
        brls::sync([]() {
            auto d = new brls::Dialog("Помилка ініціалізації мережевого запиту");
            d->addButton("OK", []() {});
            d->open();
        });
        return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, downloadUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    long response_code = 0;
    if (res == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);

    bool urlValid = (res == CURLE_OK) && (response_code == 200 || response_code == 301 || response_code == 302);

    if (urlValid) {
        brls::Logger::info("URL validated successfully, proceeding with download");
        brls::sync([this, file = std::move(file)]() mutable {
            getAppletFrame()->setHeaderVisibility(brls::Visibility::VISIBLE);
            ProgressEvent::instance().setInterupt(true);
            this->present(new DownloadView(file));
            this->stopThreadFlag = true;
        });
    } else {
        std::string errorMessage = (res != CURLE_OK)
            ? fmt::format("Не вдалося підключитися: {}", curl_easy_strerror(res))
            : fmt::format("Файл недоступний для завантаження: {}", response_code);

        brls::Logger::error("URL validation failed: {}", errorMessage);
        brls::sync([errorMessage]() {
            auto dialog = new brls::Dialog(errorMessage);
            dialog->addButton("OK", []() {});
            dialog->open();
        });
    }
}

// ─────────────────────────────────────────────
// loadImages
// ─────────────────────────────────────────────

void ModPreview::loadImages() {
    if (shouldStopThread()) return;

    size_t imageCount = std::min(int(this->mod.getImagesUrl().size()), 7);
    std::vector<SpinnerImageView*> spinnerViews;

    std::promise<void> spinnersAddedPromise;
    auto spinnersAddedFuture = spinnersAddedPromise.get_future();

    brls::sync([this, &spinnerViews, &spinnersAddedPromise, &imageCount]() {
        brls::Logger::debug("Adding spinners...");
        brls::Logger::debug("Number of image URLs: {}", imageCount);

        for (auto i = 0; i < imageCount; i += 8) {
            if (shouldStopThread()) return;

            auto box = new brls::Box();
            box->setWidth(bigImageWidth);
            box->setHeight(bigImageWidth/8 * 9/16);
            box->setAxis(brls::Axis::ROW);
            box->setJustifyContent(brls::JustifyContent::FLEX_START);
            box->setAlignItems(brls::AlignItems::FLEX_START);
            brls::Logger::debug("Box created with width: {}, height: {}", bigImageWidth, bigImageWidth/8 * 9/16);
            screenshot_box->addView(box);
            smallScreenshotsBoxs.push_back(box);
        }

        for (size_t i = 0; i < imageCount; i++) {
            if (shouldStopThread()) return;
            try {
                auto spinnerImageView = new SpinnerImageView(bigImageWidth/8, bigImageWidth/8 * 9/16, bigImageWidth/8/7/2);
                this->smallScreenshotsBoxs[i / 7]->addView(spinnerImageView);
                spinnerViews.push_back(spinnerImageView);
            } catch (const std::exception& e) {
                brls::Logger::error("Error while creating SpinnerImageView: {}", e.what());
                return;
            }
        }

        big_image_box->addView(bigSpinImg);
        brls::Logger::debug("Spinners added.");

        #ifndef NDEBUG
        cfg::Config config;
        if (config.getWireframe()) {
            this->setWireframeEnabled(true);
            for (auto& view : this->getChildren()) view->setWireframeEnabled(true);
            for (auto& view : this->smallScreenshotsBoxs) view->setWireframeEnabled(true);
            for (auto& view : this->scrolling->getChildren()) view->setWireframeEnabled(true);
        }
        #endif

        spinnersAddedPromise.set_value();
    });

    spinnersAddedFuture.wait();

    brls::Logger::debug("SpinnerViews size: {}", spinnerViews.size());

    if (spinnerViews.empty()) {
        brls::sync([this]() { if (!readOnly) loadButtons(); });
        return;
    }

    for (size_t i = 0; i < spinnerViews.size(); i++) {
        if (shouldStopThread()) return;

        std::vector<unsigned char> buffer;
        brls::Logger::debug("Downloading image {}", i);
        buffer = this->mod.downloadImage(i);
        brls::Logger::debug("Downloaded image {} with size {}", i, buffer.size());

        if (shouldStopThread()) return;

        brls::sync([this, spinnerView = spinnerViews[i], buffer, i]() mutable {
            if (shouldStopThread()) return;

            if (i == 0) {
                bigSpinImg->setImage(buffer);
                if (!readOnly) loadButtons();
            }

            brls::Logger::debug("Replacing spinner with image {}", i);
            spinnerView->setImage(buffer);
            spinnerView->registerClickAction(brls::ActionListener([this, buffer](brls::View* view) {
                this->bigSpinImg->setImage(buffer);
                return true;
            }));
        });
    }
}

// ─────────────────────────────────────────────
// Thread helpers + destructor
// ─────────────────────────────────────────────

bool ModPreview::shouldStopThread() {
    std::unique_lock<std::mutex> lock(threadMutex);
    return stopThreadFlag || isExiting;
}

void ModPreview::stopThread() {
    std::unique_lock<std::mutex> lock(threadMutex);
    stopThreadFlag = true;
    threadCondition.notify_one();
}

ModPreview::~ModPreview() {
    stopThread();
    {
        std::unique_lock<std::mutex> lock(threadMutex);
        isExiting = true;
        stopThreadFlag = true;
    }
    threadCondition.notify_one();
    if (secondThread.joinable())
        secondThread.join();
}
