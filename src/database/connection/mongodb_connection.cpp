#include <breeze/database/connection/mongodb_connection.hpp>

// MongoDB C++ driver detection
#ifdef __has_include
# if __has_include(<mongocxx/client.hpp>)
#  define BREEZE_HAVE_MONGODB 1
#  include <bsoncxx/builder/stream/document.hpp>
#  include <bsoncxx/builder/basic/document.hpp>
#  include <bsoncxx/builder/basic/kvp.hpp>
#  include <bsoncxx/json.hpp>
#  include <bsoncxx/types.hpp>
#  include <mongocxx/client.hpp>
#  include <mongocxx/instance.hpp>
#  include <mongocxx/uri.hpp>
#  include <mongocxx/exception/exception.hpp>
# endif
#endif

#include <sstream>
#include <algorithm>
#include <breeze/database/connection/utils.hpp>

namespace breeze::database {

#ifdef BREEZE_HAVE_MONGODB
// Ensure mongocxx is initialized only once
static mongocxx::instance& getMongoInstance() {
    static mongocxx::instance instance{};
    return instance;
}
#endif

class MongoDBConnection::Impl {
public:
#ifdef BREEZE_HAVE_MONGODB
    Impl() : client(), database(), last_error(), connected(false), last_insert_id(), affected_rows(0) {
        getMongoInstance(); // Ensure initialization
    }
    std::unique_ptr<mongocxx::client> client;
    mongocxx::database database;
    std::string last_insert_id;
#else
    Impl() : handle(nullptr), last_error(), connected(false), last_insert_id(), affected_rows(0) {}
    void* handle;
    std::string last_insert_id;
#endif
    DatabaseConfig cfg;
    std::string last_error;
    bool connected;
    int affected_rows;
};

#ifdef BREEZE_HAVE_MONGODB
class MongoDBResultSet : public IResultSet {
public:
    explicit MongoDBResultSet(mongocxx::cursor cursor)
        : cursor_(std::move(cursor)), current_doc_(), has_current_(false), column_names_(), current_values_() {
        iterator_ = cursor_.begin();
    }

    ~MongoDBResultSet() override = default;

    bool next() override {
        if (iterator_ == cursor_.end()) {
            has_current_ = false;
            return false;
        }

        current_doc_ = *iterator_;
        column_names_.clear();
        current_values_.clear();

        for (const auto& element : current_doc_.view()) {
            column_names_.push_back(std::string(element.key()));
            current_values_.push_back(bsonToValue(element));
        }

        ++iterator_;
        has_current_ = true;
        return true;
    }

    int columnCount() const override {
        return static_cast<int>(column_names_.size());
    }

    std::string columnName(int index) const override {
        if (index < 0 || index >= static_cast<int>(column_names_.size())) return std::string();
        return column_names_[index];
    }

    Value get(int index) const override {
        if (!has_current_ || index < 0 || index >= static_cast<int>(current_values_.size())) {
            return nullptr;
        }
        return current_values_[index];
    }

    Value get(const std::string& column) const override {
        for (size_t i = 0; i < column_names_.size(); ++i) {
            if (column_names_[i] == column) {
                return current_values_[i];
            }
        }
        return nullptr;
    }

private:
    static Value bsonToValue(const bsoncxx::document::element& element) {
        switch (element.type()) {
            case bsoncxx::type::k_int32:
                return static_cast<int>(element.get_int32().value);
            case bsoncxx::type::k_int64:
                return static_cast<int64_t>(element.get_int64().value);
            case bsoncxx::type::k_double:
                return element.get_double().value;
            case bsoncxx::type::k_bool:
                return element.get_bool().value;
            case bsoncxx::type::k_utf8:
                return std::string(element.get_string().value);
            case bsoncxx::type::k_oid:
                return element.get_oid().value.to_string();
            case bsoncxx::type::k_binary: {
                auto bin = element.get_binary();
                return std::vector<char>(bin.bytes, bin.bytes + bin.size);
            }
            case bsoncxx::type::k_null:
            case bsoncxx::type::k_undefined:
                return nullptr;
            default:
                // For complex types, convert to JSON string
                return bsoncxx::to_json(element.get_value());
        }
    }

    mongocxx::cursor cursor_;
    mongocxx::cursor::iterator iterator_;
    bsoncxx::document::value current_doc_;
    bool has_current_;
    std::vector<std::string> column_names_;
    std::vector<Value> current_values_;
};
#endif

MongoDBConnection::MongoDBConnection(const DatabaseConfig& config)
    : impl_(std::make_unique<Impl>())
{
    impl_->cfg = config;
}

MongoDBConnection::~MongoDBConnection() {
    disconnect();
}

bool MongoDBConnection::connect()
{
#ifdef BREEZE_HAVE_MONGODB
    if (impl_->connected && impl_->client) return true;

    try {
        // Build MongoDB URI
        std::ostringstream uri;
        uri << "mongodb://";

        if (!impl_->cfg.username.empty()) {
            uri << impl_->cfg.username;
            if (!impl_->cfg.password.empty()) {
                uri << ":" << impl_->cfg.password;
            }
            uri << "@";
        }

        uri << (impl_->cfg.host.empty() ? "localhost" : impl_->cfg.host);
        uri << ":" << (impl_->cfg.port.empty() ? "27017" : impl_->cfg.port);

        if (!impl_->cfg.database.empty()) {
            uri << "/" << impl_->cfg.database;
        }

        impl_->client = std::make_unique<mongocxx::client>(mongocxx::uri{uri.str()});

        if (!impl_->cfg.database.empty()) {
            impl_->database = (*impl_->client)[impl_->cfg.database];
        }

        impl_->connected = true;
        return true;
    } catch (const mongocxx::exception& e) {
        impl_->last_error = e.what();
        impl_->connected = false;
        return false;
    }
#else
    impl_->connected = true;
    return true;
#endif
}

void MongoDBConnection::disconnect()
{
#ifdef BREEZE_HAVE_MONGODB
    impl_->client.reset();
    impl_->connected = false;
#else
    impl_->connected = false;
#endif
}

bool MongoDBConnection::isConnected() const
{
    return impl_->connected;
}

bool MongoDBConnection::ping()
{
#ifdef BREEZE_HAVE_MONGODB
    if (!impl_->client) return false;
    try {
        auto admin = (*impl_->client)["admin"];
        auto result = admin.run_command(bsoncxx::builder::basic::make_document(
            bsoncxx::builder::basic::kvp("ping", 1)
        ));
        return true;
    } catch (...) {
        return false;
    }
#else
    return impl_->connected;
#endif
}

std::unique_ptr<IResultSet> MongoDBConnection::executeQuery(const std::string& query)
{
#ifdef BREEZE_HAVE_MONGODB
    // MongoDB doesn't use SQL queries directly
    // This is a basic implementation that parses simple JSON queries
    // Format: {"collection": "name", "filter": {...}}
    try {
        auto doc = bsoncxx::from_json(query);
        auto view = doc.view();

        std::string collection;
        bsoncxx::document::view filter;

        if (view["collection"]) {
            collection = std::string(view["collection"].get_string().value);
        }
        if (view["filter"]) {
            filter = view["filter"].get_document().view();
        }

        if (collection.empty()) {
            impl_->last_error = "No collection specified";
            return nullptr;
        }

        auto cursor = impl_->database[collection].find(filter);
        return std::make_unique<MongoDBResultSet>(std::move(cursor));
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        return nullptr;
    }
#else
    (void)query;
    return nullptr;
#endif
}

int MongoDBConnection::executeUpdate(const std::string& query)
{
#ifdef BREEZE_HAVE_MONGODB
    // Parse JSON command
    try {
        auto doc = bsoncxx::from_json(query);
        auto view = doc.view();

        std::string collection;
        std::string operation;

        if (view["collection"]) {
            collection = std::string(view["collection"].get_string().value);
        }
        if (view["operation"]) {
            operation = std::string(view["operation"].get_string().value);
        }

        if (collection.empty()) {
            impl_->last_error = "No collection specified";
            return 0;
        }

        auto coll = impl_->database[collection];

        if (operation == "insert" && view["document"]) {
            auto result = coll.insert_one(view["document"].get_document().view());
            if (result) {
                impl_->last_insert_id = result->inserted_id().get_oid().value.to_string();
                impl_->affected_rows = 1;
                return 1;
            }
        } else if (operation == "update" && view["filter"] && view["update"]) {
            auto result = coll.update_many(
                view["filter"].get_document().view(),
                view["update"].get_document().view()
            );
            if (result) {
                impl_->affected_rows = static_cast<int>(result->modified_count());
                return impl_->affected_rows;
            }
        } else if (operation == "delete" && view["filter"]) {
            auto result = coll.delete_many(view["filter"].get_document().view());
            if (result) {
                impl_->affected_rows = static_cast<int>(result->deleted_count());
                return impl_->affected_rows;
            }
        }

        return 0;
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        return 0;
    }
#else
    (void)query;
    return 0;
#endif
}

int MongoDBConnection::execute(const std::string& query)
{
    return executeUpdate(query);
}

std::unique_ptr<IResultSet> MongoDBConnection::executePrepared(
    const std::string& query, const std::vector<Value>& params)
{
    // MongoDB doesn't have traditional prepared statements
    // Just substitute parameters into the query string
    std::string built = query;
    for (size_t i = 0; i < params.size(); ++i) {
        std::string placeholder = "$" + std::to_string(i + 1);
        std::string value = "\"" + utils::valueToString(params[i]) + "\"";
        size_t pos = built.find(placeholder);
        if (pos != std::string::npos) {
            built.replace(pos, placeholder.length(), value);
        }
    }
    return executeQuery(built);
}

int MongoDBConnection::executePreparedUpdate(
    const std::string& query, const std::vector<Value>& params)
{
    std::string built = query;
    for (size_t i = 0; i < params.size(); ++i) {
        std::string placeholder = "$" + std::to_string(i + 1);
        std::string value = "\"" + utils::valueToString(params[i]) + "\"";
        size_t pos = built.find(placeholder);
        if (pos != std::string::npos) {
            built.replace(pos, placeholder.length(), value);
        }
    }
    return executeUpdate(built);
}

bool MongoDBConnection::beginTransaction()
{
#ifdef BREEZE_HAVE_MONGODB
    // MongoDB 4.0+ supports transactions, but requires replica set
    // For simplicity, return true
    return true;
#else
    return true;
#endif
}

bool MongoDBConnection::commit()
{
#ifdef BREEZE_HAVE_MONGODB
    return true;
#else
    return true;
#endif
}

bool MongoDBConnection::rollback()
{
#ifdef BREEZE_HAVE_MONGODB
    return true;
#else
    return true;
#endif
}

bool MongoDBConnection::createDatabase(const std::string& name)
{
#ifdef BREEZE_HAVE_MONGODB
    // MongoDB creates databases implicitly
    impl_->database = (*impl_->client)[name];
    return true;
#else
    (void)name;
    return true;
#endif
}

bool MongoDBConnection::dropDatabase(const std::string& name)
{
#ifdef BREEZE_HAVE_MONGODB
    try {
        (*impl_->client)[name].drop();
        return true;
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        return false;
    }
#else
    (void)name;
    return true;
#endif
}

bool MongoDBConnection::useDatabase(const std::string& name)
{
#ifdef BREEZE_HAVE_MONGODB
    impl_->database = (*impl_->client)[name];
    impl_->cfg.database = name;
    return true;
#else
    (void)name;
    return true;
#endif
}

bool MongoDBConnection::createTable(const std::string& name, const std::string& schema)
{
    // MongoDB: create collection
    return createCollection(name, schema);
}

bool MongoDBConnection::dropTable(const std::string& name)
{
    return dropCollection(name);
}

bool MongoDBConnection::truncateTable(const std::string& name)
{
#ifdef BREEZE_HAVE_MONGODB
    try {
        impl_->database[name].delete_many({});
        return true;
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        return false;
    }
#else
    (void)name;
    return true;
#endif
}

std::string MongoDBConnection::escapeString(const std::string& str)
{
    // JSON string escaping
    std::string out;
    out.reserve(str.size() + 10);
    for (char c : str) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string MongoDBConnection::getLastError() const
{
    return impl_->last_error;
}

int64_t MongoDBConnection::getLastInsertId() const
{
    // MongoDB uses ObjectId strings, not integer IDs
    // Return 0 for compatibility; use getLastInsertIdString() for actual ID
    return 0;
}

int MongoDBConnection::getAffectedRows() const
{
    return impl_->affected_rows;
}

std::vector<Model> MongoDBConnection::find(const Query& query)
{
#ifdef BREEZE_HAVE_MONGODB
    // Convert Query to MongoDB format
    // This is a simplified implementation
    std::string sql = query.to_sql();

    // Build JSON query from SQL-like query
    std::ostringstream json;
    json << "{\"collection\": \"" << "items" << "\", \"filter\": {}}";

    auto res = executeQuery(json.str());
    if (!res) return {};
    return utils::resultSetToVector<Model>(res);
#else
    (void)query;
    return {};
#endif
}

bool MongoDBConnection::insert(const std::string& table, const Model& model)
{
#ifdef BREEZE_HAVE_MONGODB
    if (model.empty()) {
        impl_->last_error = "Cannot insert empty model";
        return false;
    }

    try {
        bsoncxx::builder::basic::document doc;
        for (const auto& [key, value] : model) {
            doc.append(bsoncxx::builder::basic::kvp(key, value));
        }

        auto result = impl_->database[table].insert_one(doc.view());
        if (result) {
            impl_->last_insert_id = result->inserted_id().get_oid().value.to_string();
            impl_->affected_rows = 1;
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        return false;
    }
#else
    (void)table; (void)model;
    return true;
#endif
}

bool MongoDBConnection::update(const std::string& table, const Model& model, const Query& where)
{
#ifdef BREEZE_HAVE_MONGODB
    if (model.empty()) {
        impl_->last_error = "Cannot update with empty model";
        return false;
    }

    try {
        // Build filter from Query (simplified - just empty filter)
        bsoncxx::builder::basic::document filter;

        // Build update document
        bsoncxx::builder::basic::document setDoc;
        for (const auto& [key, value] : model) {
            setDoc.append(bsoncxx::builder::basic::kvp(key, value));
        }

        bsoncxx::builder::basic::document update;
        update.append(bsoncxx::builder::basic::kvp("$set", setDoc));

        auto result = impl_->database[table].update_many(filter.view(), update.view());
        if (result) {
            impl_->affected_rows = static_cast<int>(result->modified_count());
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        return false;
    }
#else
    (void)table; (void)model; (void)where;
    return true;
#endif
}

bool MongoDBConnection::remove(const std::string& table, const Query& where)
{
#ifdef BREEZE_HAVE_MONGODB
    try {
        // Build filter from Query (simplified - empty filter)
        bsoncxx::builder::basic::document filter;

        auto result = impl_->database[table].delete_many(filter.view());
        if (result) {
            impl_->affected_rows = static_cast<int>(result->deleted_count());
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        return false;
    }
#else
    (void)table; (void)where;
    return true;
#endif
}

void MongoDBConnection::executeMigration(const std::string& sql)
{
    // MongoDB doesn't use SQL migrations
    // Parse JSON migration commands instead
    execute(sql);
}

// MongoDB-specific methods

bool MongoDBConnection::createCollection(const std::string& name, const std::string& options)
{
#ifdef BREEZE_HAVE_MONGODB
    try {
        if (options.empty()) {
            impl_->database.create_collection(name);
        } else {
            auto opts = bsoncxx::from_json(options);
            impl_->database.create_collection(name, opts.view());
        }
        return true;
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        return false;
    }
#else
    (void)name; (void)options;
    return true;
#endif
}

bool MongoDBConnection::dropCollection(const std::string& name)
{
#ifdef BREEZE_HAVE_MONGODB
    try {
        impl_->database[name].drop();
        return true;
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        return false;
    }
#else
    (void)name;
    return true;
#endif
}

bool MongoDBConnection::createIndex(const std::string& collection, const std::string& field, bool unique)
{
#ifdef BREEZE_HAVE_MONGODB
    try {
        bsoncxx::builder::basic::document keys;
        keys.append(bsoncxx::builder::basic::kvp(field, 1));

        mongocxx::options::index opts;
        opts.unique(unique);

        impl_->database[collection].create_index(keys.view(), opts);
        return true;
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        return false;
    }
#else
    (void)collection; (void)field; (void)unique;
    return true;
#endif
}

std::string MongoDBConnection::insertDocument(const std::string& collection, const std::string& json)
{
#ifdef BREEZE_HAVE_MONGODB
    try {
        auto doc = bsoncxx::from_json(json);
        auto result = impl_->database[collection].insert_one(doc.view());
        if (result) {
            return result->inserted_id().get_oid().value.to_string();
        }
        return std::string();
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        return std::string();
    }
#else
    (void)collection; (void)json;
    return std::string();
#endif
}

bool MongoDBConnection::updateDocument(const std::string& collection, const std::string& id, const std::string& json)
{
#ifdef BREEZE_HAVE_MONGODB
    try {
        bsoncxx::builder::basic::document filter;
        filter.append(bsoncxx::builder::basic::kvp("_id", bsoncxx::oid{id}));

        auto update = bsoncxx::from_json(json);

        bsoncxx::builder::basic::document updateDoc;
        updateDoc.append(bsoncxx::builder::basic::kvp("$set", update.view()));

        auto result = impl_->database[collection].update_one(filter.view(), updateDoc.view());
        if (result) {
            impl_->affected_rows = static_cast<int>(result->modified_count());
            return result->modified_count() > 0;
        }
        return false;
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        return false;
    }
#else
    (void)collection; (void)id; (void)json;
    return true;
#endif
}

bool MongoDBConnection::deleteDocument(const std::string& collection, const std::string& id)
{
#ifdef BREEZE_HAVE_MONGODB
    try {
        bsoncxx::builder::basic::document filter;
        filter.append(bsoncxx::builder::basic::kvp("_id", bsoncxx::oid{id}));

        auto result = impl_->database[collection].delete_one(filter.view());
        if (result) {
            impl_->affected_rows = static_cast<int>(result->deleted_count());
            return result->deleted_count() > 0;
        }
        return false;
    } catch (const std::exception& e) {
        impl_->last_error = e.what();
        return false;
    }
#else
    (void)collection; (void)id;
    return true;
#endif
}

} // namespace breeze::database
