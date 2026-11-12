#include "workdesk/core/AIEngine.h"
#include <llama.h>
#include <QDebug>
#include <vector>
#include <string>

AIEngine::AIEngine(QObject* parent) : QObject(parent) {
    llama_backend_init();
}

AIEngine::~AIEngine() {
    unload();
    llama_backend_free();
}

bool AIEngine::loadModel(const QString& modelPath) {
    unload();
    qDebug() << "正在加载模型:" << modelPath;
    llama_model_params mparams = llama_model_default_params();
    model_ = llama_model_load_from_file(modelPath.toUtf8().constData(), mparams);
    
    if (!model_) {
        emit errorOccurred("模型加载失败");
        return false;
    }
    
    smpl_ = llama_sampler_chain_init(llama_sampler_chain_default_params());
    const llama_vocab* vocab = llama_model_get_vocab(model_);
    int32_t n_vocab = llama_vocab_n_tokens(vocab);

    llama_sampler_chain_add(smpl_, llama_sampler_init_penalties(n_vocab, 64, 1.2f, 0.0f, 0.0f));
    llama_sampler_chain_add(smpl_, llama_sampler_init_temp(0.3f)); 
    llama_sampler_chain_add(smpl_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    qDebug() << "模型加载成功！";
    emit modelLoaded(true);
    return true;
}

void AIEngine::unload() {
    if (smpl_) { llama_sampler_free(smpl_); smpl_ = nullptr; }
    if (ctx_) { llama_free(ctx_); ctx_ = nullptr; }
    if (model_) { llama_model_free(model_); model_ = nullptr; }
}

void AIEngine::resetContext() {
    // 每次推理都会重建，这里留空即可
}

void AIEngine::processPrompt(const QString& prompt) {
    if (!model_) {
        emit errorOccurred("模型未就绪");
        return;
    }
    emit thinking();

    // 【核心修复】每次提问前，重建上下文，彻底避免上次的缓存导致越界崩溃
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = 2048;       
    cparams.n_threads = 8;      
    cparams.n_batch = 512; 
    ctx_ = llama_init_from_model(model_, cparams);

    const llama_vocab* vocab = llama_model_get_vocab(model_);
    std::string prompt_str = prompt.toStdString();

    int n_tokens_max = static_cast<int>(prompt_str.length()) + 128;
    std::vector<llama_token> tokens(n_tokens_max);
    int n_tokens = llama_tokenize(vocab, prompt_str.c_str(), static_cast<int32_t>(prompt_str.length()), tokens.data(), n_tokens_max, true, false);
    
    if (n_tokens < 0) {
        n_tokens_max = -n_tokens;
        tokens.resize(n_tokens_max);
        n_tokens = llama_tokenize(vocab, prompt_str.c_str(), static_cast<int32_t>(prompt_str.length()), tokens.data(), n_tokens_max, true, false);
    }

    if (n_tokens <= 0) {
        emit errorOccurred("文本解析失败");
        return;
    }
    tokens.resize(n_tokens);

    llama_batch batch = llama_batch_init(n_tokens, 0, 1);
    batch.n_tokens = n_tokens; 
    for (int i = 0; i < n_tokens; i++) {
        batch.token[i] = tokens[i];
        batch.pos[i] = i; 
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = 0;
        batch.logits[i] = (i == n_tokens - 1);
    }

    if (llama_decode(ctx_, batch) != 0) {
        llama_batch_free(batch);
        emit errorOccurred("推理失败");
        return;
    }
    llama_batch_free(batch);

    std::string response;
    int n_cur = n_tokens;
    int n_max = n_tokens + 128; 

    while (n_cur < n_max) {
        llama_token id = llama_sampler_sample(smpl_, ctx_, -1);
        if (id == llama_vocab_eos(vocab)) break;

        char buf[256];
        int n = llama_token_to_piece(vocab, id, buf, sizeof(buf), 0, true);
        if (n > 0) {
            response.append(buf, n);
            if (response.find('\n') != std::string::npos) break;
        }

        llama_batch next_batch = llama_batch_init(1, 0, 1);
        next_batch.n_tokens = 1; 
        next_batch.token[0] = id;
        next_batch.pos[0] = n_cur;
        next_batch.n_seq_id[0] = 1;
        next_batch.seq_id[0][0] = 0;
        next_batch.logits[0] = true;

        if (llama_decode(ctx_, next_batch) != 0) {
            llama_batch_free(next_batch);
            break;
        }
        llama_batch_free(next_batch);
        n_cur++;
    }

    emit replyGenerated(QString::fromUtf8(response.c_str()).trimmed());
}