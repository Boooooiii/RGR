#pragma once

#include <pqxx/pqxx>

#include <algorithm>
#include <string>
#include <iostream>
#include <map>
#include <stdexcept>

namespace mvc {

    class Model {
    private:
        std::unique_ptr<pqxx::connection> conn;
        std::vector<std::string> tableNames;
        std::map<std::string, std::vector<std::string>> columnNames;
        std::pair<std::string, pqxx::result> tableData;

        Model();

        bool isForeignKey(const std::string& tableName, const std::string& columnName);

        const pqxx::result requestTableData(const std::string& TableName);

        std::map<std::string, std::string> getPrimaryKeyInfo(pqxx::work& worker, const std::string& tableName);

    public:

        static Model& getInstance() {
            static Model instance;
            return instance;
        }

        class Spawner {
            static Model& instance() {
                return getInstance();
            }

            friend class Controller;
        };

        Model(const Model&) = delete;
        void operator=(const Model&) = delete;

        bool isConnectionReady();

        void reconnect();

        const std::vector<std::string>& getTableColumnsNames(const std::string& TableName);

        const pqxx::result& getTableData(const std::string& TableName);

        const std::vector<std::string>& getTableNames();

        void generateRandomDataForTable(const std::string& tableName, int rowsCount);

        void deleteDataRange(const std::string& tableName, const pqxx::result& dataToDelete, int rowFrom, int rowTo);

        void updateDataCell(const std::string& tableName, pqxx::row dataRow, unsigned int changedCellNumber, const std::string& dataToChange);

        void clearAllTables(const std::string currentTableName);

        void insertData(const std::string& tableName, const std::map<std::string, std::string>& data);

        pqxx::result executeCustomQuery(const std::string& request, pqxx::params params);

    };

}