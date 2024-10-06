// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <QSize>

#include "DolphinQt2/GameList/GameListModel.h"
#include "DolphinQt2/GameList/ListProxyModel.h"

const QSize LARGE_BANNER_SIZE(144, 48);

ListProxyModel::ListProxyModel(QObject* parent) : QSortFilterProxyModel(parent)
{
  setSortCaseSensitivity(Qt::CaseInsensitive);
  sort(GameListModel::COL_TITLE);
}

QVariant ListProxyModel::data(const QModelIndex& i, int role) const
{
  QModelIndex source_index = mapToSource(i);
  if (role == Qt::DisplayRole)
  {
    return sourceModel()->data(sourceModel()->index(source_index.row(), GameListModel::COL_TITLE),
                               Qt::DisplayRole);
  }
  else if (role == Qt::DecorationRole)
  {
    auto pixmap = sourceModel()
                      ->data(sourceModel()->index(source_index.row(), GameListModel::COL_BANNER),
                             Qt::DecorationRole)
                      .value<QPixmap>();
    return pixmap.scaled(LARGE_BANNER_SIZE * pixmap.devicePixelRatio(), Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
  }
  return QVariant();
}
