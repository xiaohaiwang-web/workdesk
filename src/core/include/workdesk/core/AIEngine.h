#pragma once
#include <QObject>
#include <QString>

struct llama_model;
struct llama_context;
struct llama_sampler;

class AIEngine : public QObject {
    Q_OBJECT
public:
    explicit AIEngine(QObject* parent = nullptr);
    ~AIEngine();

    Q_INVOKABLE bool loadModel(const QString& modelPath);
    Q_INVOKABLE void resetContext(); // 【新增】
    void unload();
    bool isReady() const { return model_ != nullptr; }

public slots:
    void processPrompt(const QString& prompt);

signals:
    void modelLoaded(bool success);
    void thinking();
    void replyGenerated(const QString& reply);
    void errorOccurred(const QString& error);

private:
    struct llama_model* model_ = nullptr;
    struct llama_context* ctx_ = nullptr;
    struct llama_sampler* smpl_ = nullptr;
};