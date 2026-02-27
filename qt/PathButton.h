// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QToolButton>

class PathButton : public QToolButton
{
    Q_OBJECT

public:
    enum Mode : uint8_t
    {
        DirectoryMode,
        FileMode
    };

    explicit PathButton(QWidget* parent = nullptr);
    ~PathButton() override = default;
    PathButton(PathButton&&) = delete;
    PathButton(PathButton const&) = delete;
    PathButton& operator=(PathButton&&) = delete;
    PathButton& operator=(PathButton const&) = delete;

    void set_mode(Mode mode);
    void set_title(QString const& title);
    void set_name_filter(QString const& name_filter);

    void set_path(QString const& path);

    [[nodiscard]] constexpr auto const& path() const noexcept
    {
        return path_;
    }

    // QWidget
    [[nodiscard]] QSize sizeHint() const override;

signals:
    void path_changed(QString const& path);

protected:
    // QWidget
    void paintEvent(QPaintEvent* event) override;

private slots:
    void on_clicked() const;
    void on_file_selected(QString const& path);

private:
    void update_appearance();

    [[nodiscard]] bool is_dir_mode() const;
    [[nodiscard]] QString effective_title() const;

    QString name_filter_;
    QString path_;
    QString title_;
    Mode mode_ = DirectoryMode;
};
