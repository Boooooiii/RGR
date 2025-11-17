#include "Model.h"

using namespace mvc;

Model::Model() {
    try {
        std::string cS = "host=localhost port=8040 dbname=Lab1 user=postgres password=4242";
        conn = std::make_unique<pqxx::connection>(cS);

        pqxx::work worker(*conn);
        pqxx::result res = worker.exec("SELECT tablename FROM pg_tables WHERE schemaname = 'public' ORDER BY tablename;");

        tableNames.clear();
        for (auto row : res) {
            tableNames.push_back(row[0].as<std::string>());
        }
    }
    catch (const std::exception& e) {
        throw std::runtime_error(e.what());
    }

}

bool Model::isForeignKey(const std::string& tableName, const std::string& columnName)
{
    try {
        pqxx::nontransaction worker(*conn);

        const std::string sql =
            "SELECT 1 FROM information_schema.table_constraints AS tc "
            "JOIN information_schema.key_column_usage AS kcu ON tc.constraint_name = kcu.constraint_name AND tc.table_schema = kcu.table_schema "
            "WHERE tc.constraint_type = 'FOREIGN KEY' AND tc.table_schema = " + worker.quote_name("public") +
            "  AND tc.table_name = " + worker.quote_name(tableName) + " AND kcu.column_name = " + worker.quote_name(columnName) + " LIMIT 1;";

        pqxx::result res = worker.exec(sql);

        return !res.empty();
    }
    catch (const std::exception& e) {
        throw std::runtime_error(e.what());
        return false;
    }
}

const pqxx::result Model::requestTableData(const std::string& TableName) {
    if (!conn)
        reconnect();
    try {
        pqxx::work worker(*conn);
        pqxx::result res = worker.exec("SELECT * FROM " + worker.quote_name(TableName));
        tableData = { TableName, res };
        return tableData.second;
    }
    catch (const std::exception& e) {
        throw std::runtime_error(e.what());
    }
}

std::map<std::string, std::string> Model::getPrimaryKeyInfo(pqxx::work& worker, const std::string& tableName) {
    std::string pk_query =
        "SELECT kcu.column_name, c.data_type "
        "FROM information_schema.table_constraints AS tc "
        "JOIN information_schema.key_column_usage AS kcu "
        "  ON tc.constraint_name = kcu.constraint_name AND tc.table_schema = kcu.table_schema "
        "JOIN information_schema.columns AS c "
        "  ON kcu.table_name = c.table_name AND kcu.table_schema = c.table_schema AND kcu.column_name = c.column_name "
        "WHERE tc.constraint_type = 'PRIMARY KEY' "
        "  AND tc.table_name = " + worker.quote(tableName) +
        " ORDER BY kcu.ordinal_position;";

    pqxx::result pk_res = worker.exec(pk_query);

    std::map<std::string, std::string> pkInfo;
    for (const auto& row : pk_res) {
        pkInfo[row[0].as<std::string>()] = row[1].as<std::string>();
    }
    return pkInfo; 
}

bool Model::isConnectionReady() {
    if (!conn)
        return false;
    return true;
}

void Model::reconnect() {
    try {
        std::string cS = "host=localhost port=8040 dbname=Lab1 user=postgres password=4242";
        conn = std::make_unique<pqxx::connection>(cS);
    }
    catch (const std::exception& e) {
        throw std::runtime_error(e.what());
    }
}
    
const std::vector<std::string>& Model::getTableNames() {
    return tableNames;
}

const std::vector<std::string>& Model::getTableColumnsNames(const std::string& TableName) {

    if (!conn)
        reconnect();

    auto it = columnNames.find(TableName);
    if (it != columnNames.end())
        return it->second;

    std::vector<std::string> names;
    try {
        pqxx::work worker(*conn);
        pqxx::result res = worker.exec("SELECT column_name FROM information_schema.columns WHERE table_name = " + worker.quote(TableName) +
            " AND table_schema = 'public' ORDER BY ordinal_position;");

        for (const auto& row : res)
            names.push_back(row[0].as<std::string>());

    }
    catch (const std::exception& e) {
        throw std::runtime_error(e.what());
        return names;
    }

    columnNames[TableName] = names;

    return columnNames.at(TableName);
}

const pqxx::result& Model::getTableData(const std::string& TableName) {
    if (tableData.first != TableName)
        return requestTableData(TableName);
    return tableData.second;
}

void Model::generateRandomDataForTable(const std::string& tableName, int rowsCount) {
    try {
        if (!conn)
            reconnect();

        pqxx::work txn(*conn);

        if (tableName.find("teacher_subject") != std::string::npos)
        {
            // Перевіряємо, чи є вхідні дані
            pqxx::result t_count = txn.exec("SELECT COUNT(*) FROM Teacher;");
            pqxx::result s_count = txn.exec("SELECT COUNT(*) FROM Subject;");
            if (t_count[0][0].as<int>() == 0 || s_count[0][0].as<int>() == 0) {
                throw std::runtime_error("cannot generate links - Teacher or Subject table is empty.");
            }

            std::string insertSQL =
                "INSERT INTO teacher_subject (teacher_id, subject_id) "
                "SELECT t.teacher_id, s.subject_id "
                "FROM Teacher t "
                "CROSS JOIN Subject s "
                "LEFT JOIN teacher_subject ts "
                "  ON t.teacher_id = ts.teacher_id AND s.subject_id = ts.subject_id "
                "WHERE ts.teacher_id IS NULL " 
                "ORDER BY random() "
                "LIMIT $1"; 

            txn.exec(insertSQL, rowsCount);
            txn.commit();
            requestTableData(tableName);
            return;
        }

        pqxx::result fks = txn.exec("(SELECT kcu.column_name, ccu.table_name AS foreign_table, ccu.column_name AS foreign_column "
            "FROM information_schema.table_constraints AS tc JOIN information_schema.key_column_usage AS kcu ON tc.constraint_name = kcu.constraint_name "
            "JOIN information_schema.constraint_column_usage AS ccu ON ccu.constraint_name = tc.constraint_name "
            "WHERE tc.table_name = " + txn.quote(tableName) + " AND tc.constraint_type = 'FOREIGN KEY' AND tc.table_schema = 'public')");

        std::vector<std::string> missingTables;
        for (auto const& row : fks) {
            std::string refTable = row["foreign_table"].c_str();
            pqxx::result count = txn.exec("SELECT COUNT(*) FROM " + txn.quote_name(refTable) + ";");
            if (count[0][0].as<int>() == 0)
                missingTables.push_back(refTable);
        }

        if (!missingTables.empty()) {
            std::string msg = "missing data in related tables: ";
            for (size_t i = 0; i < missingTables.size(); ++i) {
                msg += missingTables[i] + (i + 1 < missingTables.size() ? ", " : "");
            }
            throw std::runtime_error(msg);
        }

        pqxx::result all_cols = txn.exec("(SELECT column_name, data_type, character_maximum_length, column_default FROM information_schema.columns "
            "WHERE table_name = " + txn.quote(tableName) + " AND table_schema = 'public' ORDER BY ordinal_position)");

        std::string colsList, valsList;
        std::vector<pqxx::row> colsToInsert;

        for (auto const& row : all_cols) {
            if (row["column_default"].is_null()) {
                colsToInsert.push_back(row);
            }
        }

        if (colsToInsert.empty()) {
            throw std::runtime_error("No columns to insert");
        }

        for (size_t i = 0; i < colsToInsert.size(); ++i) {
            colsList += txn.quote_name(colsToInsert[i]["column_name"].c_str());
            if (i + 1 < colsToInsert.size()) colsList += ", ";
        }


        for (size_t i = 0; i < colsToInsert.size(); ++i) {
            std::string colName = colsToInsert[i]["column_name"].c_str();
            std::string colType = colsToInsert[i]["data_type"].c_str();
            auto colLength = colsToInsert[i]["character_maximum_length"];
            bool isFK = false;
            std::string fkSelect;

            for (auto const& fk : fks) {
                if (fk["column_name"].c_str() == colName) {
                    isFK = true;
                    fkSelect = "(SELECT " + txn.quote_name(fk["foreign_column"].c_str()) +
                        " FROM " + txn.quote_name(fk["foreign_table"].c_str()) +
                        " ORDER BY random() LIMIT 1 OFFSET (s.i * 0))";

                    break;
                }
            }

            if (isFK) {
                valsList += fkSelect;
            }
            else if (colType.find("char") != std::string::npos || colType == "text") {

                std::string generator_sql;
                bool apply_substr = true;

                if (colName.find("first_name") != std::string::npos) {
                    generator_sql = reinterpret_cast<const char*>(u8"(ARRAY['Іван', 'Петро', 'Марія', 'Ярослав', 'Анна', 'Володимир', "
                        u8"'Василь', 'Ганна', 'Вікторія', 'Анастасія', 'Денис', 'Артем', 'Руслан', 'Михайло', 'Микита'])[floor(random() * 15 + 1)]");
                }
                else if (colName.find("last_name") != std::string::npos) {
                    generator_sql = reinterpret_cast<const char*>(u8"(ARRAY['Шевченко', 'Коваленко', 'Петренко', 'Іваненко', 'Бойко', 'Мельник', "
                        u8"'Одарченко', 'Порошенко', 'Мироненко', 'Сектим', 'Гончаренко', 'Винник', 'Боженко', 'Файнберг'])[floor(random() * 14 + 1)]");
                }
                else if (colName.find("status") != std::string::npos) {
                    generator_sql = reinterpret_cast<const char*>(u8"(ARRAY['Присутній', 'Відсутній'])[floor(random() * 2 + 1)]");
                }
                else if (colName.find("class_type") != std::string::npos) {
                    generator_sql = reinterpret_cast<const char*>(u8"(ARRAY['Практика', 'Лекція', 'Лабораторна'])[floor(random() * 3 + 1)]");
                }
                else if (colName.find("email") != std::string::npos) {
                    generator_sql = "substr(md5(random()::text), 1, 5) || '.' || substr(md5(random()::text), 1, 7) || "
                        "(ARRAY['@gmail.com', '@ukr.net', '@kpi.ua'])[floor(random() * 3 + 1)]";
                }
                else if (colName.find("group_name") != std::string::npos) {
                    generator_sql = reinterpret_cast<const char*>(u8"(ARRAY['КВ', 'КМ', 'КП'])[floor(random() * 3 + 1)] || '-' || "
                        "floor(random() * 90000 + 10000)::int || '-' || gen_random_uuid()::text");
                }
                else if (colName.find("subject_name") != std::string::npos) {
                    generator_sql = reinterpret_cast<const char*>(u8"(ARRAY['СОНС', 'АМО', 'КЕ', 'ЗІКС', 'БДЗУ', 'КС'])[floor(random() * 6 + 1)] "
                        "|| '-' || gen_random_uuid()::text");
                }
                else if (colName.find("faculty") != std::string::npos) {
                    generator_sql = reinterpret_cast<const char*>(u8"(ARRAY['ФПМ', 'ФМФ', 'ФІОТ'])[floor(random() * 3 + 1)]");
                }


                else {
                    std::string len = "8";
                    if (!colLength.is_null()) {
                        int dbLen = colLength.as<int>();
                        len = (std::to_string)((std::min)(dbLen, 32)); 
                    }
                    generator_sql = "substr(md5(random()::text), 1, " + len + ")";
                    apply_substr = false; 
                }


                if (apply_substr && !colLength.is_null()) {
                    valsList += "substr(" + generator_sql + ", 1, " + colLength.as<std::string>() + ")";
                }
                else {
                    valsList += generator_sql;
                }
            }
            else if (colType.find("int") != std::string::npos) {
                valsList += "floor(random()*1000)::int";
            }
            else if (colType.find("date") != std::string::npos || colType.find("timestamp") != std::string::npos) {
                valsList += "now() - (random() * interval '10 years')";
            }
            else if (colType == "boolean") {
                valsList += "random() > 0.5";
            }
            else {
                valsList += "NULL";
            }

            if (i + 1 < colsToInsert.size()) valsList += ", ";
        }

        std::string insertSQL = "INSERT INTO " + txn.quote_name(tableName) +
            " (" + colsList + ") " +
            " SELECT " + valsList +
            " FROM generate_series(1, " + std::to_string(rowsCount) + ") AS s(i);";

        txn.exec(insertSQL.c_str()).no_rows();

        txn.commit();
        requestTableData(tableName);
    }
    catch (std::exception const& e) {
        throw std::runtime_error(e.what());
    }
}

void Model::deleteDataRange(const std::string& tableName, const pqxx::result& dataToDelete, int rowFrom, int rowTo)
{
    if (rowFrom > rowTo) {
        std::swap(rowFrom, rowTo);
    }
    if (dataToDelete.empty()) {
        throw std::runtime_error("No data to delete");
    }

    std::string tempTableName = "temp_pks_to_delete_" + std::to_string(std::time(nullptr));

    try {
        pqxx::work worker(*conn);

        std::map<std::string, std::string> pkInfo = getPrimaryKeyInfo(worker, tableName);
        if (pkInfo.empty()) {
            throw std::runtime_error("No PK in table");
        }

        std::vector<int> pkIndicesInResult;
        std::string pkColsForCreate;

        std::vector<std::string> pkColNamesForCopy;

        std::string pkColsForJoin;

        try {
            for (const auto& pair : pkInfo) {
                const std::string& colName = pair.first;
                const std::string& colType = pair.second;

                pkIndicesInResult.push_back(dataToDelete.column_number(colName));
                std::string quotedName = worker.quote_name(colName);

                pkColsForCreate += quotedName + " " + colType + ",";
                pkColNamesForCopy.push_back(colName);
                pkColsForJoin += "t." + quotedName + " = tmp." + quotedName + " AND ";
            }
        }
        catch (const std::exception& e) {
            throw std::runtime_error("Can not find PK column: " + std::string(e.what()));
        }

        pkColsForCreate.pop_back();
        pkColsForJoin.resize(pkColsForJoin.size() - 5);

        std::string createTempTableSql = "CREATE TEMPORARY TABLE " + worker.quote_name(tempTableName) +
            " (" + pkColsForCreate + ") ON COMMIT DROP;";
        worker.exec(createTempTableSql);

        std::string full = tempTableName + "(";
        for (size_t i = 0; i < pkColNamesForCopy.size(); ++i)
        {
            full += worker.quote_name(pkColNamesForCopy[i]);
            if (i + 1 != pkColNamesForCopy.size()) full += ",";
        }
        full += ")";

        auto copier = pqxx::stream_to::raw_table(worker, full);

        for (int i = rowFrom; i <= rowTo; ++i) {
            const auto& row = dataToDelete[i];
            std::vector<std::string> values;
            for (size_t j = 0; j < pkIndicesInResult.size(); ++j) {
                const auto& field = row[pkIndicesInResult[j]];
                if (field.is_null()) {
                    values.push_back("\\N");
                }
                else {
                    values.push_back(field.c_str());
                }
            }
            copier.write_values(values);
        }
        copier.complete();

        std::string deleteSql = "DELETE FROM " + worker.quote_name(tableName) + " t "
            "USING " + worker.quote_name(tempTableName) + " tmp "
            "WHERE " + pkColsForJoin;
        worker.exec(deleteSql);

        worker.commit();
        requestTableData(tableName);
    }
    catch (const std::exception& e) {
        throw std::runtime_error("Deleting data error: " + std::string(e.what()));
    }
}

void Model::updateDataCell(const std::string& tableName, pqxx::row dataRow, unsigned int changedCellNumber, const std::string& dataToChange) {
    if (dataRow.size() <= 0) {
        throw std::runtime_error("Nothing to update?");
        return;
    }
    try {
        std::vector<std::string> colNames = getTableColumnsNames(tableName);

        if (colNames[changedCellNumber].find("status") != std::string::npos && tableName.find("attendance") != std::string::npos) {
            if (dataToChange != "Присутній" && dataToChange != "Відсутній") {
                throw std::runtime_error("Wrong status! Should be 'Присутній' or 'Відсутній'");
            }
        }

        pqxx::work worker(*conn);
        std::string request = "UPDATE " + worker.quote_name(tableName) + " SET " + colNames[changedCellNumber] + " = " + worker.quote(dataToChange) + " WHERE ";
        unsigned int i = 0;
        for (const auto& element : dataRow) {
            if (i != changedCellNumber) {
                request += worker.quote_name(colNames[i]);

                if (element.is_null()) {
                    request += " IS NULL AND ";
                }
                else {
                    request += " = " + worker.quote(element.as<std::string>()) + " AND ";
                }
            }
            i++;
        }
        if (request.size() >= 5) request.resize(request.size() - 5);

        worker.exec(request);
        worker.commit();
        requestTableData(tableName);
    }
    catch (const std::exception& e) {
        throw std::runtime_error(e.what());
    }
}

void Model::clearAllTables(const std::string currentTableName) {
    try {
        pqxx::work worker(*conn);
        std::string request = "TRUNCATE TABLE ";

        std::vector<std::string> names = getTableNames();
        for (const auto& name : names)
            request += name + ", ";
        request.pop_back();
        request.pop_back();
        request += " RESTART IDENTITY CASCADE;";

        worker.exec(request);
        worker.commit();
        requestTableData(currentTableName);
    }
    catch (const std::exception& e) {
        throw std::runtime_error(e.what());
    }
}

void Model::insertData(const std::string& tableName, const std::map<std::string, std::string>& data) {
    if (data.empty())
        throw std::runtime_error("No data to insert");

    try {
        pqxx::work worker(*conn);
        std::string request = "INSERT INTO " + worker.quote_name(tableName) + " (";
        std::string cols = "", vals = "";
        for (const auto& pair : data) {
            if (pair.first.find("status") != std::string::npos && tableName.find("attendance") != std::string::npos) {
                if (pair.second != "Присутній" && pair.second != "Відсутній") {
                    throw std::runtime_error("Wrong status! Should be 'Присутній' or 'Відсутній'");
                }
            }
            cols += worker.quote_name(pair.first) + ",";
            vals += worker.quote(pair.second) + ",";
        }
        cols.pop_back();
        vals.pop_back();
        request += cols + ") VALUES (" + vals + ")";

        worker.exec(request);
        worker.commit();
        requestTableData(tableName);
    }
    catch (const std::exception& e) {
        throw std::runtime_error(e.what());
    }
}

pqxx::result Model::executeCustomQuery(const std::string& request, pqxx::params params) {
    try {
        pqxx::work worker(*conn);
        return worker.exec(request, params);
    }
    catch (const std::exception& e) {
        throw std::runtime_error(e.what());
    }
}