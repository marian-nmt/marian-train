#include "marian.h"

#include "common/regex.h"
#include "models/model_factory.h"
#include "models/encoder_decoder.h"
#include "models/encoder_classifier.h"
#include "models/bert.h"

#include "models/costs.h"

#include "models/amun.h"
#include "models/nematus.h"
#include "models/s2s.h"
#include "models/transformer_factory.h"

#ifdef CUDNN
#include "models/char_s2s.h"
#endif

#ifdef COMPILE_EXAMPLES
#include "examples/mnist/model.h"
#ifdef CUDNN
#include "examples/mnist/model_lenet.h"
#endif
#endif

#include "models/classifier.h"

namespace marian {
namespace models {

Ptr<EncoderBase> EncoderFactory::construct(Ptr<ExpressionGraph> graph) {
  if(options_->get<std::string>("type") == "s2s")
    return New<EncoderS2S>(options_);

#ifdef CUDNN
  if(options_->get<std::string>("type") == "char-s2s")
    return New<CharS2SEncoder>(options_);
#endif

  if(options_->get<std::string>("type") == "transformer")
    return NewEncoderTransformer(options_);

  if(options_->get<std::string>("type") == "bert-encoder")
    return New<BertEncoder>(options_);

  ABORT("Unknown encoder type");
}

Ptr<DecoderBase> DecoderFactory::construct(Ptr<ExpressionGraph> graph) {
  if(options_->get<std::string>("type") == "s2s")
    return New<DecoderS2S>(options_);
  if(options_->get<std::string>("type") == "transformer")
    return NewDecoderTransformer(options_);
  ABORT("Unknown decoder type");
}

Ptr<ClassifierBase> ClassifierFactory::construct(Ptr<ExpressionGraph> /*graph*/) {
  if(options_->get<std::string>("type") == "bert-masked-lm")
    return New<BertMaskedLM>(options_);
  if(options_->get<std::string>("type") == "bert-classifier")
    return New<BertClassifier>(options_);
  ABORT("Unknown classifier type");
}

Ptr<ModelBase> EncoderDecoderFactory::construct(Ptr<ExpressionGraph> graph) {
  Ptr<EncoderDecoder> encdec;

  if(options_->get<std::string>("type") == "amun")
    encdec = New<Amun>(options_);
  if(options_->get<std::string>("type") == "nematus")
    encdec = New<Nematus>(options_);

  if(!encdec)
    encdec = New<EncoderDecoder>(options_);

  for(auto& ef : encoders_)
    encdec->push_back(ef(options_).construct(graph));

  for(auto& df : decoders_)
    encdec->push_back(df(options_).construct(graph));

  return add_cost(encdec, options_);
}

Ptr<ModelBase> EncoderClassifierFactory::construct(Ptr<ExpressionGraph> graph) {
  Ptr<EncoderClassifier> enccls;
  if(options_->get<std::string>("type") == "bert") {
    enccls = New<BertEncoderClassifier>(options_);
  } else if(options_->get<std::string>("type") == "bert-classifier") {
    enccls = New<BertEncoderClassifier>(options_);
  } else {
    enccls = New<EncoderClassifier>(options_);
  }

  for(auto& ef : encoders_)
    enccls->push_back(ef(options_).construct(graph));

  for(auto& cf : classifiers_)
    enccls->push_back(cf(options_).construct(graph));

  return add_cost(enccls, options_);
}

Ptr<ModelBase> by_type(std::string type, usage use, Ptr<Options> options) {
  Ptr<ExpressionGraph> graph = nullptr; // graph unknown at this stage
  // clang-format off
  if(type == "s2s" || type == "amun" || type == "nematus") {
    return models::encoder_decoder()(options)
        ("usage", use)
        ("original-type", type)
            .push_back(models::encoder()("type", "s2s"))
            .push_back(models::decoder()("type", "s2s"))
            .construct(graph);
  }

  if(type == "transformer") {
    return models::encoder_decoder()(options)
        ("usage", use)
        .push_back(models::encoder()("type", "transformer"))
        .push_back(models::decoder()("type", "transformer"))
        .construct(graph);
  }

  if(type == "transformer_s2s") {
    return models::encoder_decoder()(options)
        ("usage", use)
        ("original-type", type)
            .push_back(models::encoder()("type", "transformer"))
            .push_back(models::decoder()("type", "s2s"))
            .construct(graph);
  }

  if(type == "lm") {
    auto idx = options->has("index") ? options->get<size_t>("index") : 0;
    std::vector<int> dimVocabs = options->get<std::vector<int>>("dim-vocabs");
    int vocab = dimVocabs[0];
    dimVocabs.resize(idx + 1);
    std::fill(dimVocabs.begin(), dimVocabs.end(), vocab);

    return models::encoder_decoder()(options)
        ("usage", use)
        ("type", "s2s")
        ("original-type", type)
            .push_back(models::decoder()
                       ("index", idx)
                       ("dim-vocabs", dimVocabs))
            .construct(graph);
  }

  if(type == "multi-s2s") {
    size_t numEncoders = 2;
    auto ms2sFactory = models::encoder_decoder()(options)
        ("usage", use)
        ("type", "s2s")
        ("original-type", type);

    for(size_t i = 0; i < numEncoders; ++i) {
      auto prefix = "encoder" + std::to_string(i + 1);
      ms2sFactory.push_back(models::encoder()("prefix", prefix)("index", i));
    }

    ms2sFactory.push_back(models::decoder()("index", numEncoders));

    return ms2sFactory.construct(graph);
  }

  if(type == "shared-multi-s2s") {
    size_t numEncoders = 2;
    auto ms2sFactory = models::encoder_decoder()(options)
        ("usage", use)
        ("type", "s2s")
        ("original-type", type);

    for(size_t i = 0; i < numEncoders; ++i) {
      auto prefix = "encoder";
      ms2sFactory.push_back(models::encoder()("prefix", prefix)("index", i));
    }

    ms2sFactory.push_back(models::decoder()("index", numEncoders));

    return ms2sFactory.construct(graph);
  }

  if(type == "multi-transformer") {
    size_t numEncoders = 2;
    auto mtransFactory = models::encoder_decoder()(options)
        ("usage", use)
        ("type", "transformer")
        ("original-type", type);

    for(size_t i = 0; i < numEncoders; ++i) {
      auto prefix = "encoder" + std::to_string(i + 1);
      mtransFactory.push_back(models::encoder()("prefix", prefix)("index", i));
    }
    mtransFactory.push_back(models::decoder()("index", numEncoders));

    return mtransFactory.construct(graph);
  }

  if(type == "shared-multi-transformer") {
    size_t numEncoders = 2;
    auto mtransFactory = models::encoder_decoder()(options)
        ("usage", use)
        ("type", "transformer")
        ("original-type", type);

    for(size_t i = 0; i < numEncoders; ++i) {
      auto prefix = "encoder";
      mtransFactory.push_back(models::encoder()("prefix", prefix)("index", i));
    }
    mtransFactory.push_back(models::decoder()("index", numEncoders));

    return mtransFactory.construct(graph);
  }

  if(type == "lm-transformer") {
    auto idx = options->has("index") ? options->get<size_t>("index") : 0;
    std::vector<int> dimVocabs = options->get<std::vector<int>>("dim-vocabs");
    int vocab = dimVocabs[0];
    dimVocabs.resize(idx + 1);
    std::fill(dimVocabs.begin(), dimVocabs.end(), vocab);

    return models::encoder_decoder()(options)
        ("usage", use)
        ("type", "transformer")
        ("original-type", type)
            .push_back(models::decoder()
                       ("index", idx)
                       ("dim-vocabs", dimVocabs))
            .construct(graph);
  }

  if(type == "bert") {                           // for full BERT training
    return models::encoder_classifier()(options) //
        ("original-type", "bert")                // so we can query this
        ("usage", use)                           //
        .push_back(models::encoder()             //
                    ("type", "bert-encoder")     // close to original transformer encoder
                    ("index", 0))                //
        .push_back(models::classifier()          //
                    ("prefix", "masked-lm")      // prefix for parameter names
                    ("type", "bert-masked-lm")   //
                    ("index", 0))                // multi-task learning with MaskedLM
        .push_back(models::classifier()          //
                    ("prefix", "next-sentence")  // prefix for parameter names
                    ("type", "bert-classifier")  //
                    ("index", 1))                // next sentence prediction
        .construct(graph);
  }

  if(type == "bert-classifier") {                // for BERT fine-tuning on non-BERT classification task
    return models::encoder_classifier()(options) //
        ("original-type", "bert-classifier")     // so we can query this if needed
        ("usage", use)                           //
        .push_back(models::encoder()             //
                    ("type", "bert-encoder")     //
                    ("index", 0))                // close to original transformer encoder
        .push_back(models::classifier()          //
                    ("type", "bert-classifier")  //
                    ("index", 1))                // next sentence prediction
        .construct(graph);
  }

  if(type == "bert-gpt") {
    
    auto gpt = models::encoder_decoder()(options)
        ("usage", use)
        ("index", 1)                 // use stream 1
        ("type", "transformer")
        ("original-type", "bert-gpt")
        .push_back(models::decoder()
                   ("prefix", "decoder"))        //
        .construct(graph);

    if(use != usage::training) // if not training
      return gpt; // only evaluate target LM

    auto model = New<MultiModel<SumMultiRationalLoss>>();
    auto bert = models::encoder_classifier()(options) //
        ("type", "bert")                         //
        ("original-type", "bert-gpt")            // so we can query this
        ("index", 0)                             //
        ("usage", use)                           //
        .push_back(models::encoder()             //
                  ("type", "bert-encoder")       // close to original transformer encoder
                  ("prefix", "encoder"))         //
        .push_back(models::classifier()          //
                  ("prefix", "masked-lm")        // prefix for parameter names
                  ("type", "bert-masked-lm"))    // multi-task learning with MaskedLM
        .construct(graph);

    model->push_back(bert);  
    model->push_back(gpt);

    return model;
  }

  if(type == "transformer-bert-gpt") {
    type = "transformer12-bert0"; // @TODO: remove hacky hack!
  }

  if(regex::regex_match(type, regex::regex("transformer(\\d\\d)?(-(bert|gpt)(\\d?))+"))) {
    auto parts = utils::split(type, "-");
    
    size_t encoderStream = 0;
    size_t decoderStream = 1;

    {
      regex::smatch match;
      regex::regex_match(parts[0], match, regex::regex("(transformer)(\\d)?(\\d)?"));
      std::string subtype = match[1].str();
      std::string streamEnc  = match[2].str();
      std::string streamDec  = match[3].str();
      encoderStream = streamEnc.empty() ? 0 : std::stoi(streamEnc);
      decoderStream = streamDec.empty() ? 1 : std::stoi(streamDec);
      LOG_ONCE(info, "Model {} using streams {} and {}", subtype, encoderStream, decoderStream);
    }
    
    auto transformer = models::encoder_decoder()(options)
        ("usage", use)
        ("original-type", type) // so we can query this
        .push_back(models::encoder()
                   ("type", "transformer")
                   ("prefix", "encoder")
                   ("index", encoderStream)) // use stream 0
        .push_back(models::decoder()
                   ("type", "transformer")
                   ("prefix", "decoder")
                   ("index", decoderStream)) // use stream 1
        .construct(graph);

    if(use != usage::training) // only build main translator for translation and scoring
      return transformer;

    auto model = New<MultiModel<SumMultiRationalLoss>>();
    model->push_back(transformer);

    for(int i = 1; i < parts.size(); ++i) {
      regex::smatch match;
      regex::regex_match(parts[i], match, regex::regex("(bert|gpt)(\\d)?"));
      std::string subtype = match[1].str();
      std::string stream  = match[2].str();
      size_t streamNo = stream.empty() ? (2 + i - 1) : std::stoi(stream);

      LOG_ONCE(info, "Model {} using stream {}", subtype, streamNo);

      if(subtype == "bert") {
        auto bert = models::encoder_classifier()(options)
            ("type", "bert")                          //
            ("original-type", type)                   // so we can query this
            ("index", streamNo)                       // Index for BertBatch
            ("usage", use)                            //
            .push_back(models::encoder()              //
                      ("type", "bert-encoder")        // close to original transformer encoder
                      ("prefix", "encoder"))          //
            .push_back(models::classifier()           //
                      ("prefix", "masked-lm")         // prefix for parameter names
                      ("type", "bert-masked-lm")      // multi-task learning with MaskedLM
                      ("label-smoothing", 0))         // no label-smoothing for masked LM
            .construct(graph);
        model->push_back(bert);
      } else if(subtype == "gpt") {
        auto gpt = models::encoder_decoder()(options)
            ("type", "transformer")
            ("original-type", type)
            ("index", streamNo)
            ("usage", use)
            .push_back(models::decoder()
                      ("prefix", "decoder"))
            .construct(graph);
        model->push_back(gpt);
      } else {
        ABORT("Unknown model subtype {}", subtype);
      }
    }
  
    return model;
  }

  // @TODO: clean up code duplication
  if(regex::regex_match(type, regex::regex("smtransformer(-(bert|gpt)(\\d?))+"))) {
    // transformer translation model, always on streams 0 (encoder) and 1 (decoder)
    auto transformer = models::encoder_decoder()(options)
        ("usage", use)
        ("original-type", type) // so we can query this
        .push_back(models::encoder()
                   ("type", "transformer")
                   ("prefix", "encoder")
                   ("index", 0)) // use stream 0
        .push_back(models::encoder()
                   ("type", "transformer")
                   ("prefix", "encoder")
                   ("index", 1)) // use stream 1
        .push_back(models::decoder()
                   ("type", "transformer")
                   ("prefix", "decoder")
                   ("index", 2)) // use stream 2
        .construct(graph);

    if(use != usage::training) // only build main translator for translation and scoring
      return transformer;

    auto model = New<MultiModel<SumMultiRationalLoss>>();
    model->push_back(transformer);

    auto parts = utils::split(type, "-");
    for(int i = 1; i < parts.size(); ++i) {
      regex::smatch match;
      regex::regex_match(parts[i], match, regex::regex("(bert|gpt)(\\d)?"));
      std::string subtype = match[1].str();
      std::string stream  = match[2].str();
      size_t streamNo = stream.empty() ? (3 + i - 1) : std::stoi(stream);

      if(subtype == "bert") {
        auto bert = models::encoder_classifier()(options)
            ("type", "bert")                          //
            ("original-type", type) // so we can query this
            ("index", streamNo)                       // Index for BertBatch
            ("usage", use)                            //
            .push_back(models::encoder()              //
                      ("type", "bert-encoder")        // close to original transformer encoder
                      ("prefix", "encoder"))          //
            .push_back(models::classifier()           //
                      ("prefix", "masked-lm")         // prefix for parameter names
                      ("type", "bert-masked-lm")      // multi-task learning with MaskedLM
                      ("label-smoothing", 0))         // no label-smoothing for masked LM
            .construct(graph);
        model->push_back(bert);
      } else if(subtype == "gpt") {
        auto gpt = models::encoder_decoder()(options)
            ("type", "transformer")
            ("original-type", type)
            ("index", streamNo)
            ("usage", use)
            .push_back(models::decoder()
                      ("prefix", "decoder"))
            .construct(graph);
        model->push_back(gpt);
      } else {
        ABORT("Unknown model subtype {}", subtype);
      }
    }
  
    return model;
  }

#ifdef COMPILE_EXAMPLES
  if(type == "mnist-ffnn") {
    auto mnist = New<MnistFeedForwardNet>(options);
    if(use == usage::scoring)
      return New<Scorer>(mnist, New<MNISTLogsoftmax>());
    else if(use == usage::training)
      return New<Trainer>(mnist, New<MNISTCrossEntropyCost>());
    else
      return mnist;
  }
#endif

#ifdef CUDNN
#ifdef COMPILE_EXAMPLES
  if(type == "mnist-lenet") {
    auto mnist = New<MnistLeNet>(options);
    if(use == usage::scoring)
      return New<Scorer>(mnist, New<MNISTLogsoftmax>());
    else if(use == usage::training)
      return New<Trainer>(mnist, New<MNISTCrossEntropyCost>());
    else
      return mnist;
  }
#endif
  if(type == "char-s2s") {
    return models::encoder_decoder()(options)
        ("usage", use)
        ("original-type", type)
            .push_back(models::encoder()("type", "char-s2s"))
            .push_back(models::decoder()("type", "s2s"))
            .construct(graph);
  }
#endif

  // clang-format on
  ABORT("Unknown model type: {}", type);
}

Ptr<ModelBase> from_options(Ptr<Options> options, usage use) {
  std::string type = options->get<std::string>("type");
  return by_type(type, use, options);
}

}  // namespace models
}  // namespace marian
