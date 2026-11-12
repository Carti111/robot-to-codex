#pragma once

enum class SparkPage { Chat, Clock, Codex, Muyu, Dice, Game, Camera, Count };

// A side touch only previews another page while dictation is engaged.
struct SparkMenuState {
    SparkPage page = SparkPage::Chat;
    int preview = -1;
    unsigned long long preview_deadline = 0;
    bool exiting = false;
    SparkPage target = SparkPage::Chat;

    void ClearPreview() {
        preview = -1;
        preview_deadline = 0;
    }

    bool Navigate(int direction, bool engaged, unsigned long long now) {
        if (exiting) return false;
        if (preview >= 0 && now >= preview_deadline) ClearPreview();
        int base = preview >= 0 ? preview : static_cast<int>(page);
        int count = static_cast<int>(SparkPage::Count);
        int next = (base + direction + count) % count;
        if (engaged) {
            preview = next;
            preview_deadline = now + 3000;
            return false;
        }
        page = static_cast<SparkPage>(next);
        ClearPreview();
        return true;
    }

    bool Confirm(bool engaged, unsigned long long now) {
        if (preview < 0 || now >= preview_deadline) {
            ClearPreview();
            return false;
        }
        target = static_cast<SparkPage>(preview);
        ClearPreview();
        if (target == page) return false;
        if (engaged) {
            exiting = true;
            return false;
        }
        page = target;
        return true;
    }

    bool Tick(bool engaged, unsigned long long now) {
        if (preview >= 0 && now >= preview_deadline) ClearPreview();
        if (exiting && !engaged) {
            exiting = false;
            page = target;
            return true;
        }
        return false;
    }
};
