#include "locationsmodel.h"
#include "locationsmodel_utils.h"
#include "../locationsmodel_roles.h"
#include "languagecontroller.h"

namespace gui_locations {

LocationsModel::LocationsModel(QObject *parent) : QAbstractItemModel(parent), isFreeSessionStatus_(false)
{
    root_ = new int();
    favoriteLocationsStorage_.readFromSettings();

    connect(&LanguageController::instance(), &LanguageController::languageChanged, this, &LocationsModel::onLanguageChanged);
}

LocationsModel::~LocationsModel()
{
    clearLocations();
    delete root_;
}

void LocationsModel::updateLocations(const LocationID &bestLocation, const QVector<types::Location> &newLocations)
{
    if (locations_.empty())
    {
        // just copy the list if the first update
        beginResetModel();
        int i = 0;
        for (const auto &l : newLocations)
        {
            LocationItem *li = new LocationItem(l);
            locations_ << li;
            mapLocations_[l.id] = li;
            i++;
        }
        endResetModel();
    }
    else
    {
        const utils::LocationsVector newLocationsVector(newLocations);

        QVector<int> removedInds = utils::findRemovedLocations(locations_, newLocationsVector);
        for (int i = removedInds.size() - 1; i >= 0; i--)
        {
            int removedInd = removedInds[i];
            beginRemoveRows(QModelIndex(), removedInd, removedInd);
            mapLocations_.remove(locations_[removedInd]->location().id);
            delete (locations_[removedInd]);
            locations_.removeAt(removedInd);
            endRemoveRows();
        }

        QVector<int> newInds = utils::findNewLocations(mapLocations_, newLocationsVector);
        int bestLocationOffs = 0;
        if (locations_.size() > 0 && locations_[0]->location().id.isBestLocation())
        {
            bestLocationOffs = 1;
        }
        for (auto i : newInds)
        {
            beginInsertRows(QModelIndex(), i + bestLocationOffs, i + bestLocationOffs);
            LocationItem *li = new LocationItem(newLocationsVector[i]);
            mapLocations_[li->location().id] = li;
            locations_.insert(i + bestLocationOffs, li);
            endInsertRows();
        }

        QVector<QPair<int, int> > changedInds = utils::findChangedLocations(locations_, newLocationsVector);
        for (const auto &i : changedInds)
        {
            handleChangedLocation(i.first, newLocationsVector[i.second]);
        }

        bool isFoundMovedLocations;
        QVector<int> locationsInds = utils::findMovedLocations(locations_, newLocationsVector, isFoundMovedLocations);
        if (isFoundMovedLocations)
        {
            // Selection sort algorithm
            for (int i = 0; i < locationsInds.size(); i++)
            {
                int minz = locationsInds[i];
                int ind = i;
                for (int j = i + 1; j < locationsInds.size(); j++)
                {
                    if (locationsInds[j] < minz)
                    {
                        minz = locationsInds[j];
                        ind = j;
                    }
                }
                if (i != ind)
                {
                    beginMoveRows(QModelIndex(), ind, ind, QModelIndex(), i);
                    locationsInds.move(ind, i);
                    locations_.move(ind, i);
                    endMoveRows();
                }
            }
        }
    }
    updateBestLocation(bestLocation);
}

void LocationsModel::updateBestLocation(const LocationID &bestLocation)
{
    if (locations_.isEmpty()) {
        return;
    }
    LocationItem *liBestLocation = findAndCreateBestLocationItem(bestLocation);
    LocationID firstLocationId = locations_[0]->location().id;
    if (firstLocationId.isBestLocation())
    {
        if (liBestLocation)
        {
            // change the best location only if it has actually changed
            if (*liBestLocation != *locations_[0])
            {
                delete locations_[0];
                mapLocations_.remove(firstLocationId);
                locations_[0] = liBestLocation;
                mapLocations_[liBestLocation->location().id] = liBestLocation;

                emit dataChanged(index(0, 0), index(0, 0));
            }
            else
            {
                delete liBestLocation;
            }
        }
        else
        {
            // delete the best location (first item) because the best location is not found
            beginRemoveRows(QModelIndex(), 0, 0);
            mapLocations_.remove(firstLocationId);
            delete locations_[0];
            locations_.remove(0);
            endRemoveRows();
        }
    }
    else
    {
        // insert the best location to the top of the list
        if (liBestLocation)
        {
            beginInsertRows(QModelIndex(), 0, 0);
            locations_.insert(0, liBestLocation);
            mapLocations_[liBestLocation->location().id] = liBestLocation;
            endInsertRows();
        }
    }
    onLanguageChanged();
}

void LocationsModel::updateCustomConfigLocation(const types::Location &location)
{
    // check if the custom location already inserted in the list
    LocationID lid = LocationID::createTopCustomConfigsLocationId();
    auto it = mapLocations_.find(lid);
    if (it != mapLocations_.end())
    {
        WS_ASSERT(locations_[locations_.size() - 1] == it.value());
        if (location.id.isValid())
        {
            WS_ASSERT(location.id.isCustomConfigsLocation());
            handleChangedLocation(locations_.size() - 1, location);
        }
        else
        {
            beginRemoveRows(QModelIndex(), locations_.size() - 1, locations_.size() - 1);
            mapLocations_.remove(lid);
            delete locations_[locations_.size() - 1];
            locations_.remove(locations_.size() - 1);
            endRemoveRows();
        }
    }
    else
    {
        if (location.id.isValid())
        {
            WS_ASSERT(location.id.isCustomConfigsLocation());
            // Insert custom config location to the end
            beginInsertRows(QModelIndex(), locations_.size(), locations_.size());
            LocationItem *li = new LocationItem(location);
            locations_ << li;
            mapLocations_[lid] = li;
            endInsertRows();
        }
    }
}

void LocationsModel::changeConnectionSpeed(LocationID id, PingTime speed)
{
    auto it = mapLocations_.find(id.toTopLevelLocation());
    if (it != mapLocations_.end())
    {
        int ind = locations_.indexOf(it.value());
        WS_ASSERT(ind != -1);
        if (ind != -1)
        {
            for (int c = 0; c < it.value()->location().cities.size(); ++c)
            {
                if (it.value()->location().cities[c].id == id)
                {
                    it.value()->setPingTimeForCity(c, speed);
                    QModelIndex locationModelInd = index(ind, 0);
                    emit dataChanged(locationModelInd, locationModelInd, QList<int>() << kPingTime);
                    QModelIndex cityModelInd = index(c, 0, locationModelInd);
                    emit dataChanged(cityModelInd, cityModelInd, QList<int>() << kPingTime);
                    break;
                }
            }
        }
    }

    if (locations_.size() > 0)
    {
        // update speed for best location
        if (!id.isCustomConfigsLocation() && !id.isStaticIpsLocation() && locations_[0]->location().id == id.apiLocationToBestLocation())
        {
            locations_[0]->setPingTimeForCity(0, speed);
            emit dataChanged(index(0, 0), index(0, 0), QList<int>() << kPingTime);
        }
    }
}

void LocationsModel::setFreeSessionStatus(bool isFreeSessionStatus)
{
    if (isFreeSessionStatus != isFreeSessionStatus_)
    {
        isFreeSessionStatus_ = isFreeSessionStatus;
        emit dataChanged(index(0, 0), index(locations_.size() - 1, 0));
        for (int i = 0; i < locations_.size(); ++i)
        {
            if (locations_[i]->location().cities.size() > 0)
            {
                QModelIndex parentInd = index(i, 0);
                emit dataChanged(index(0, 0, parentInd), index(locations_[i]->location().cities.size() - 1, 0, parentInd));
            }
        }
    }
}

int LocationsModel::columnCount(const QModelIndex &/*parent*/) const
{
    return 1;
}

QVariant LocationsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    if (index.internalPointer() == (void *)root_)
    {
        return dataForLocation(index.row(), role);
    }
    else
    {
        return dataForCity((LocationItem *)index.internalPointer(), index.row(), role);
    }
}

QModelIndex LocationsModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
    {
        return QModelIndex();
    }

    if (!parent.isValid())
    {
        if (row >= 0 && row < locations_.size() && column == 0)
        {
            return createIndex(row, column, (void *)root_);
        }
    }
    else
    {
        WS_ASSERT(parent.internalPointer() != nullptr);
        if ((int *)parent.internalPointer() == root_ && parent.row() >= 0 && parent.row() < locations_.size())
        {
            LocationItem *li = locations_[parent.row()];
            if (row >= 0 && row < li->location().cities.size()  && column == 0)
            {
                return createIndex(row, column, (void *)li);
            }
        }
    }

    return QModelIndex();
}

QModelIndex LocationsModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
    {
        return QModelIndex();
    }
    if (index.internalPointer() == (void *)root_)
    {
        return QModelIndex();
    }

    LocationItem *li = (LocationItem *)index.internalPointer();
    int ind = locations_.indexOf(li);
    WS_ASSERT(ind != -1);
    if (ind != -1)
    {
        return createIndex(ind, 0, (void *)root_);
    }

    return QModelIndex();
}

int LocationsModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
    {
        return locations_.size();
    }
    else
    {
        if (parent.internalPointer() == root_)
        {
            return locations_[parent.row()]->location().cities.size();
        }
    }

    return 0;
}

bool LocationsModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role == kIsFavorite)
    {
        if (!index.isValid())
        {
            return false;
        }
        // can't set favorite for country
        if (index.internalPointer() == (void *)root_)
        {
            return false;
        }
        else
        {
            LocationItem *li = (LocationItem *)index.internalPointer();
            LocationID lid = li->location().cities[index.row()].id;
            if (value.toBool() == true)
            {
                favoriteLocationsStorage_.addToFavorites(lid);
            }
            else
            {
                favoriteLocationsStorage_.removeFromFavorites(lid);
            }
            emit dataChanged(index, index, QList<int>() << kIsFavorite);
            return true;
        }
    }
    return false;
}

Qt::ItemFlags LocationsModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }

    bool isItemEnabled;
    LocationID lid = qvariant_cast<LocationID>(index.data(kLocationId));
    if (lid.isTopLevelLocation()) {
        isItemEnabled = rowCount(index) > 0;
    }
    else if (lid.isCustomConfigsLocation()) {
        isItemEnabled = index.data(kIsCustomConfigCorrect).toBool();
    }
    else {
        isItemEnabled = !index.data(kIsDisabled).toBool() &&  !index.data(kIsShowAsPremium).toBool();
    }
    if (isItemEnabled) {
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    }
    else {
        return Qt::ItemIsSelectable;
    }
}

QModelIndex LocationsModel::getIndexByLocationId(const LocationID &id) const
{
    LocationID lid;

    if (!id.isValid()) {
        return QModelIndex();
    }

    if (id.isBestLocation()) {
        auto it = mapLocations_.find(id);
        if (it != mapLocations_.end()) {
            int ind = locations_.indexOf(it.value());
            return index(ind, 0);
        }
        // Best location not found.  It's possible this location was a best location but is no longer.
        // Try it as a regular location
        lid = id.bestLocationToApiLocation();
    } else {
        lid = id;
    }

    auto it = mapLocations_.find(lid.toTopLevelLocation());
    if (it != mapLocations_.end()) {
        int ind = locations_.indexOf(it.value());

        if (lid.isTopLevelLocation()) {
            return index (ind, 0);
        } else {
            LocationItem *li = it.value();
            for (int c = 0; c < li->location().cities.size(); ++c) {
                if (li->location().cities[c].id == lid) {
                    QModelIndex locationModelInd = index(ind, 0);
                    QModelIndex cityModelInd = index(c, 0, locationModelInd);
                    return cityModelInd;
                }
            }
        }
    }

    return QModelIndex();
}

QModelIndex LocationsModel::getBestLocationIndex() const
{
    // best location always on top
    if (locations_.size() > 0)
    {
        if (locations_[0]->location().id.isBestLocation())
        {
            return index(0, 0);
        }
    }
    return QModelIndex();
}

QModelIndex LocationsModel::getCustomConfigLocationIndex() const
{
    // the custom configs location should be at the end of the list, so we start the cycle from the end
    for (int i = locations_.size() - 1; i >= 0; --i)
        if (locations_[i]->location().id.isCustomConfigsLocation())
            return index(i, 0);

    return QModelIndex();
}

void LocationsModel::saveFavoriteLocations()
{
    favoriteLocationsStorage_.writeToSettings();
}

QVariant LocationsModel::dataForLocation(int row, int role) const
{
    if (role == Qt::DisplayRole)
    {
        return locations_[row]->location().name;
    }
    else if (role == kIsTopLevelLocation)
    {
        return true;
    }
    else if (role == kName)
    {
        return locations_[row]->location().name;
    }
    else if (role == kNick)
    {
        return locations_[row]->nickname();
    }
    else if (role == kLocationId)
    {
        QVariant stored;
        stored.setValue(locations_[row]->location().id);
        return stored;
    }
    else if (role == kCountryCode)
    {
        return locations_[row]->location().countryCode.toLower();
    }
    else if (role == kIsShowP2P)
    {
        return locations_[row]->location().isNoP2P;
    }
    else if (role == kIsShowAsPremium)
    {
        return locations_[row]->location().isPremiumOnly && isFreeSessionStatus_;
    }
    else if (role == kIs10Gbps)
    {
        return locations_[row]->is10gbps();
    }
    else if (role == kLoad)
    {
        return locations_[row]->load();
    }
    else if (role == kPingTime)
    {
        return locations_[row]->averagePing();
    }
    else if (role == kIsDisabled)
    {
        if (locations_[row]->location().id.isBestLocation()) {
            return false;
        }
        else {
            return locations_[row]->location().cities.isEmpty();
        }
    }

    return QVariant();
}

QVariant LocationsModel::dataForCity(LocationItem *l, int row, int role) const
{
    if (role == Qt::DisplayRole)
    {
        if (l->location().cities[row].id.isStaticIpsLocation()) {
            return l->location().cities[row].city + " - " + l->location().cities[row].staticIp;
        }
        else  {
            return l->location().cities[row].city + " - " + l->location().cities[row].nick;
        }
    }
    else if (role == kLocationId)
    {
        QVariant stored;
        stored.setValue(l->location().cities[row].id);
        return stored;
    }
    else if (role == kIsTopLevelLocation)
    {
        return false;
    }
    else if (role == kCountryCode)
    {
        LocationID lid = l->location().cities[row].id;
        if (lid.isStaticIpsLocation())
        {
            return l->location().cities[row].staticIpCountryCode.toLower();
        }
        else
        {
            return l->location().countryCode.toLower();
        }
    }
    else if (role == kIs10Gbps)
    {
        return l->location().cities[row].is10Gbps;
    }
    else if (role == kLoad)
    {
        // Engine is using -1 to indicate to us that the load (health) value was invalid/missing
        int cityHealth = l->location().cities[row].health;
        if (cityHealth >= 0 && cityHealth <= 100)
        {
            return cityHealth;
        }
        else
        {
            return 0;
        }
    }
    else if (role == kName)
    {
        return l->location().cities[row].city;
    }
    else if (role == kNick)
    {
        LocationID lid = l->location().cities[row].id;
        if (lid.isStaticIpsLocation())
            return l->location().cities[row].staticIp;
        else
            return l->location().cities[row].nick;
    }
    else if (role == kPingTime)
    {
        return l->location().cities[row].pingTimeMs.toInt();
    }
    else if (role == kIsShowAsPremium)
    {
        LocationID lid = l->location().cities[row].id;
        if (lid.isStaticIpsLocation())
            return false;
        else
            return l->location().cities[row].isPro && isFreeSessionStatus_;
    }
    else if (role == kIsFavorite)
    {
        LocationID lid = l->location().cities[row].id;
        return favoriteLocationsStorage_.isFavorite(lid);
    }
    else if (role == kStaticIpType)
    {
        return l->location().cities[row].staticIpType;
    }
    else if (role == kStaticIp)
    {
        return l->location().cities[row].staticIp;
    }
    else if (role == kIsDisabled)
    {
        return l->location().cities[row].isDisabled;
    }
    else if (role == kIsCustomConfigCorrect)
    {
        return l->location().cities[row].customConfigIsCorrect;
    }
    else if (role == kCustomConfigType)
    {
        if (l->location().cities[row].customConfigType == CUSTOM_CONFIG_OPENVPN)
        {
            return "ovpn";
        }
        else if (l->location().cities[row].customConfigType == CUSTOM_CONFIG_WIREGUARD)
        {
            return "wg";
        }
        else
        {
            WS_ASSERT(false);
        }
    }
    else if (role == kCustomConfigErrorMessage)
    {
        return l->location().cities[row].customConfigErrorMessage;
    }

    return QVariant();
}

void LocationsModel::clearLocations()
{
    for (auto it : qAsConst(locations_))
    {
        delete it;
    }
    locations_.clear();
    mapLocations_.clear();
}

void LocationsModel::handleChangedLocation(int ind, const types::Location &newLocation)
{
    QModelIndex rootIndex = index(ind, 0);
    LocationItem *li = locations_[ind];

    const utils::CitiesVector citiesVector(newLocation.cities);

    QVector<int> removedCitiesInds = utils::findRemovedCities(li->location().cities, citiesVector);
    for (int i = removedCitiesInds.size() - 1; i >= 0; i--)
    {
        int removedCityInd = removedCitiesInds[i];
        beginRemoveRows(rootIndex, removedCityInd, removedCityInd);
        li->removeCityAtInd(removedCityInd);
        endRemoveRows();
    }

    QVector<int> newCitiesInds = utils::findNewCities(li->location().cities, citiesVector);
    for (auto i : newCitiesInds)
    {
        beginInsertRows(rootIndex, i, i);
        li->insertCityAtInd(i, citiesVector[i]);
        endInsertRows();
    }

    WS_ASSERT(li->location().cities.size() == newLocation.cities.size());
    QVector<QPair<int, types::City> > changedCitiesInds = utils::findChangedCities(li->location().cities, citiesVector);
    for (const auto &i : changedCitiesInds)
    {
        li->updateCityAtInd(i.first, i.second);
        QModelIndex cityInd = index(i.first, 0, rootIndex);
        emit dataChanged(cityInd, cityInd);
    }

    bool isMovedCitiesFound;
    QVector<int> citiesInds = utils::findMovedCities(li->location().cities, citiesVector, isMovedCitiesFound);
    if (isMovedCitiesFound)
    {
        // Selection sort algorithm
        for (int i = 0; i < citiesInds.size(); i++)
        {
            int minz = citiesInds[i];
            int ind = i;
            for (int j = i + 1; j < citiesInds.size(); j++)
            {
                if (citiesInds[j] < minz)
                {
                    minz = citiesInds[j];
                    ind = j;
                }
            }
            if (i != ind)
            {
                beginMoveRows(rootIndex, ind, ind, rootIndex, i);
                citiesInds.move(ind, i);
                li->moveCity(ind, i);
                endMoveRows();
            }
        }
    }

    li->updateLocation(newLocation);
    emit dataChanged(rootIndex, rootIndex);
}

LocationItem *LocationsModel::findAndCreateBestLocationItem(const LocationID &bestLocation)
{
    if (!bestLocation.isValid()) {
        return nullptr;
    }

    auto it = mapLocations_.find(bestLocation.bestLocationToApiLocation().toTopLevelLocation());
    if (it != mapLocations_.end())
    {
        LocationItem *li = it.value();
        for (int c = 0; c < li->location().cities.size(); ++c)
        {
            LocationID cityLid = li->location().cities[c].id;
            if (bestLocation == cityLid.apiLocationToBestLocation())
            {
                LocationItem *liBestLocation = new LocationItem(bestLocation, li->location(), c);
                liBestLocation->setName(tr(BEST_LOCATION_NAME));
                return liBestLocation;
            }
        }
    }
    return nullptr;
}

void LocationsModel::onLanguageChanged()
{
    if (locations_.isEmpty() || !locations_[0]->location().id.isBestLocation()) {
        return;
    }

    locations_[0]->setName(tr(BEST_LOCATION_NAME));
    emit dataChanged(index(0, 0), index(0, 0), QList<int>() << kName);
}

} //namespace gui_locations
